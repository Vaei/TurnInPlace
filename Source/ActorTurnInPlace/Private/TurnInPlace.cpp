// Copyright (c) Jared Taylor. All Rights Reserved


#include "TurnInPlace.h"

#include "GameplayTagContainer.h"
#include "TurnInPlaceAnimInterface.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_EDITOR
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif

#if WITH_SIMPLE_ANIMATION && UE_ENABLE_DEBUG_DRAWING
#include "SimpleAnimLib.h"
#endif

#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlace)

DEFINE_LOG_CATEGORY_STATIC(LogTurnInPlace, Log, All);

#define LOCTEXT_NAMESPACE "TurnInPlaceComponent"

namespace TurnInPlaceCvars
{
#if UE_ENABLE_DEBUG_DRAWING
	static bool bDebugTurnOffset = false;
	FAutoConsoleVariableRef CVarDebugTurnOffset(
		TEXT("p.Turn.Debug.TurnOffset"),
		bDebugTurnOffset,
		TEXT("Draw TurnOffset on screen"),
		ECVF_Default);

	static bool bDebugTurnOffsetArrow = false;
	FAutoConsoleVariableRef CVarDebugTurnOffsetArrow(
		TEXT("p.Turn.Debug.TurnOffset.Arrow"),
		bDebugTurnOffsetArrow,
		TEXT("Draw GREEN debug arrow showing the direction of the turn offset"),
		ECVF_Default);

	static bool bDebugActorDirectionArrow = false;
	FAutoConsoleVariableRef CVarDebugActorDirectionArrow(
		TEXT("p.Turn.Debug.ActorDirection.Arrow"),
		bDebugActorDirectionArrow,
		TEXT("Draw PINK debug arrow showing the direction the actor rotation is facing"),
		ECVF_Default);

	static bool bDebugControlDirectionArrow = false;
	FAutoConsoleVariableRef CVarDebugControlDirectionArrow(
		TEXT("p.Turn.Debug.ControlDirection.Arrow"),
		bDebugControlDirectionArrow,
		TEXT("Draw BLACK debug arrow showing the direction the control rotation is facing"),
		ECVF_Default);
#endif
}

UTurnInPlace::UTurnInPlace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsValidAnimInstance(false)
	, bWarnIfAnimInterfaceNotImplemented(true)
	, bHasWarned(false)
	, TurnOffset(0)
	, CurveValue(0)
	, InterpOutAlpha(0)
	, bLastUpdateValidCurveValue(false)
{
	// We don't need to tick
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	
	// Replicate the turn offset to simulated proxies
	SetIsReplicatedByDefault(true);
}

void UTurnInPlace::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// Push Model
	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;
	SharedParams.Condition = COND_SimulatedOnly;

	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, SimulatedTurnOffset, SharedParams);
}

ENetRole UTurnInPlace::GetLocalRole() const
{
	return IsValid(Character) ? Character->GetLocalRole() : ROLE_None;
}

bool UTurnInPlace::HasAuthority() const
{
	return IsValid(Character) ? Character->HasAuthority() : false;
}

void UTurnInPlace::CompressSimulatedTurnOffset(float LastTurnOffset)
{
	// Compress result and replicate turn offset to simulated proxy
	if (HasAuthority() && GetNetMode() != NM_Standalone)
	{
		const FQuat LastTurnQuat = FRotator(0.f, LastTurnOffset, 0.f).Quaternion();
		const FQuat CurrentTurnQuat = FRotator(0.f, TurnOffset, 0.f).Quaternion();
		if (!CurrentTurnQuat.Equals(LastTurnQuat, TURN_ROTATOR_TOLERANCE))
		{
			SimulatedTurnOffset.Compress(TurnOffset);
			MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, SimulatedTurnOffset, this);
		}
	}
}

void UTurnInPlace::OnRep_SimulatedTurnOffset()
{
	// Decompress the replicated value from short to float, and apply it to the TurnInPlace component
	// This keeps simulated proxies in sync with the server and allows them to turn in place
	if (GetLocalRole() == ROLE_SimulatedProxy && HasValidData())
	{
		TurnOffset = SimulatedTurnOffset.Decompress();
	}
}

void UTurnInPlace::OnRegister()
{
	Super::OnRegister();
	
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		SetUpdatedCharacter();
	}
}

void UTurnInPlace::PostLoad()
{
	Super::PostLoad();
	SetUpdatedCharacter();
}

void UTurnInPlace::InitializeComponent()
{
	Super::InitializeComponent();
	SetUpdatedCharacter();
}

void UTurnInPlace::SetUpdatedCharacter()
{
	// Cache the owning character
	Character = IsValid(GetOwner()) ? Cast<ACharacter>(GetOwner()) : nullptr;
}

void UTurnInPlace::BeginPlay()
{
	Super::BeginPlay();

	// Bind to the Mesh event to detect when the AnimInstance changes so we can recache it and check if it implements UTurnInPlaceAnimInterface
	if (ensureAlways(IsValid(Character)))
	{
		if (GetMesh())
		{
			if (GetMesh()->OnAnimInitialized.IsBound())
			{
				GetMesh()->OnAnimInitialized.RemoveDynamic(this, &ThisClass::OnAnimInstanceChanged);
			}
			GetMesh()->OnAnimInitialized.AddDynamic(this, &ThisClass::OnAnimInstanceChanged);
			OnAnimInstanceChanged();
		}
	}
}

void UTurnInPlace::DestroyComponent(bool bPromoteChildren)
{
	// Unbind from the Mesh's AnimInstance event
	if (GetMesh())
	{
		if (GetMesh()->OnAnimInitialized.IsBound())
		{
			GetMesh()->OnAnimInitialized.RemoveDynamic(this, &ThisClass::OnAnimInstanceChanged);
		}
	}
	
	Super::DestroyComponent(bPromoteChildren);
}

void UTurnInPlace::OnAnimInstanceChanged()
{
	// Cache the AnimInstance and check if it implements UTurnInPlaceAnimInterface
	AnimInstance = GetMesh()->GetAnimInstance();
	bIsValidAnimInstance = false;
	if (IsValid(AnimInstance))
	{
		// Check if the AnimInstance implements the TurnInPlaceAnimInterface and cache the result so we don't have to check every frame
		bIsValidAnimInstance = AnimInstance->Implements<UTurnInPlaceAnimInterface>();
		if (!bIsValidAnimInstance && bWarnIfAnimInterfaceNotImplemented && !bHasWarned)
		{
			// Log a warning if the AnimInstance does not implement the TurnInPlaceAnimInterface
			bHasWarned = true;
			const FText ErrorMsg = FText::Format(
				LOCTEXT("InvalidAnimInstance", "The anim instance {0} assigned to {1} on {2} does not implement the TurnInPlaceAnimInterface."),
				FText::FromString(AnimInstance->GetClass()->GetName()), FText::FromString(GetMesh()->GetName()), FText::FromString(GetName()));
#if WITH_EDITOR
			// Show a notification in the editor
			FNotificationInfo Info(FText::FromString("Invalid Turn In Place Setup. See Message Log."));
			Info.ExpireDuration = 6.f;
			FSlateNotificationManager::Get().AddNotification(Info);

			// Log the error to message log
			FMessageLog("PIE").Error(ErrorMsg);
#else
			// Log the error to the output log
			UE_LOG(LogTurnInPlaceCharacter, Error, TEXT("%s"), *ErrorMsg.ToString());
#endif
		}
	}
}

bool UTurnInPlace::IsTurningInPlace() const
{
	// We are turning in place if the weight curve is not 0
	return HasValidData() && !FMath::IsNearlyZero(GetCurveValues().TurnYawWeight, KINDA_SMALL_NUMBER);
}

USkeletalMeshComponent* UTurnInPlace::GetMesh_Implementation() const
{
	return Character ? Character->GetMesh() : nullptr;
}

bool UTurnInPlace::IsCharacterStationary() const
{
	return Character->GetVelocity().IsNearlyZero();
}

UAnimMontage* UTurnInPlace::GetCurrentNetworkRootMotionMontage() const
{
	// Check if the character is playing a networked root motion montage
	if (bIsValidAnimInstance && Character && Character->IsPlayingNetworkedRootMotionMontage())
	{
		// Get the root motion montage instance and return the montage
		if (const FAnimMontageInstance* MontageInstance = AnimInstance->GetRootMotionMontageInstance())
		{
			return MontageInstance->Montage;
		}
	}
	return nullptr;
}

bool UTurnInPlace::ShouldIgnoreRootMotionMontage_Implementation(const UAnimMontage* Montage) const
{
	if (!HasValidData())
	{
		return false;
	}

	FTurnInPlaceParams Params = GetParams();

	// Check if the montage itself is ignored
	if (Params.IgnoreMontages.Contains(Montage))
	{
		return true;
	}

	// We generally don't want to consider any montages that are additive as playing a montage
	if (Params.bIgnoreAdditiveMontages && Montage->IsValidAdditive())
	{
		return true;
	}

	// Check if any montage anim tracks ignore this slot
	for (const FName& Slot : Params.IgnoreMontageSlots)
	{
		if (Montage->IsValidSlot(Slot))
		{
			return true;
		}
	}
	
	return false;
}

ETurnInPlaceOverride UTurnInPlace::OverrideTurnInPlace_Implementation() const
{
	// We want to pause turn in place when using root motion montages
	if (UAnimMontage* Montage = GetCurrentNetworkRootMotionMontage())
	{
		// But we don't want to pause turn in place if the montage is ignored by our current params
		if (ShouldIgnoreRootMotionMontage(Montage))
		{
			return ETurnInPlaceOverride::ForcePaused;
		}
	}
	
	return ETurnInPlaceOverride::Default;
}

FGameplayTag UTurnInPlace::GetTurnModeTag_Implementation() const
{
	// Determine the turn mode tag based on the character's movement settings
	const bool bIsStrafing = Character && Character->GetCharacterMovement() && !Character->GetCharacterMovement()->bOrientRotationToMovement;
	return bIsStrafing ? FTurnInPlaceTags::TurnMode_Strafe : FTurnInPlaceTags::TurnMode_Movement;
}

ETurnInPlaceEnabledState UTurnInPlace::GetEnabledState(const FTurnInPlaceParams& Params) const
{
	if (!HasValidData())
	{
		return ETurnInPlaceEnabledState::Locked;
	}

	// Determine the enabled state of turn in place
	// This allows us to lock or pause turn in place, or force it to be enabled based on runtime conditions
	ETurnInPlaceEnabledState State = Params.State;
	ETurnInPlaceOverride OverrideState = OverrideTurnInPlace();
	switch (OverrideState)
	{
	case ETurnInPlaceOverride::Default: return State;
	case ETurnInPlaceOverride::ForceEnabled: return ETurnInPlaceEnabledState::Enabled;
	case ETurnInPlaceOverride::ForceLocked: return ETurnInPlaceEnabledState::Locked;
	case ETurnInPlaceOverride::ForcePaused: return ETurnInPlaceEnabledState::Paused;
	default: return State;
	}
}

FTurnInPlaceParams UTurnInPlace::GetParams() const
{
	if (!HasValidData())
	{
		return {};
	}

	// Get the current turn in place parameters from the animation blueprint
	FTurnInPlaceAnimSet AnimSet = ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceAnimSet(AnimInstance);
	return AnimSet.Params;
}

FTurnInPlaceCurveValues UTurnInPlace::GetCurveValues() const
{
	if (!HasValidData())
	{
		return {};
	}

	// Get the current turn in place curve values from the animation blueprint
	return ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceCurveValues(AnimInstance);
}

bool UTurnInPlace::HasValidData() const
{
	// We need a valid AnimInstance and Character to proceed, and the anim instance must implement the TurnInPlaceAnimInterface
	return bIsValidAnimInstance && IsValid(Character) && !Character->IsPendingKillPending() && Character->GetCharacterMovement();
}

ETurnMethod UTurnInPlace::GetTurnMethod() const
{
	if (!HasValidData())
	{
		return ETurnMethod::None;
	}

	// ACharacter::FaceRotation handles turn in place when bOrientRotationToMovement is false, and we orient to control rotation
	// This is an instant snapping turn that rotates to control rotation
	if (!Character->GetCharacterMovement()->bOrientRotationToMovement)
	{
		if (Character->bUseControllerRotationPitch || Character->bUseControllerRotationYaw || Character->bUseControllerRotationRoll)
		{
			return ETurnMethod::FaceRotation;
		}
	}

	// UCharacterMovementComponent::PhysicsRotation handles orienting rotation to movement or controller desired rotation
	// This is a smooth rotation that interpolates to the desired rotation
	return ETurnMethod::PhysicsRotation;
}

void UTurnInPlace::TurnInPlace(const FRotator& CurrentRotation, const FRotator& DesiredRotation)
{
	// Determine the correct params to use
	FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	ETurnInPlaceEnabledState State = GetEnabledState(Params);
	
	// Turn in place is locked, we can't do anything
	const bool bEnabled = State != ETurnInPlaceEnabledState::Locked;
	if (!bEnabled)
	{
		TurnOffset = 0.f;
		CurveValue = 0.f;
		return;
	}

	// Reset it here, because we are not appending, and this accounts for velocity being applied (no turn in place)
	TurnOffset = 0.f;
	InterpOutAlpha = 0.f;

	// If turn in place is paused, we can't accumulate any turn offset
	if (State != ETurnInPlaceEnabledState::Paused)
	{
		TurnOffset = (DesiredRotation - CurrentRotation).GetNormalized().Yaw;
	}

	// Apply any turning from the animation sequence
	float LastCurveValue = CurveValue;
	FTurnInPlaceCurveValues CurveValues = GetCurveValues();
	const float TurnYawWeight = CurveValues.TurnYawWeight;

	if (FMath::IsNearlyZero(TurnYawWeight, KINDA_SMALL_NUMBER))
	{
		// No curve weight, don't apply any animation yaw
		CurveValue = 0.f;
		bLastUpdateValidCurveValue = false;
	}
	else
	{
		// Apply the remaining yaw from the current animation (curve) that is playing, scaled by the weight curve
		const float RemainingTurnYaw = CurveValues.RemainingTurnYaw;
		CurveValue = RemainingTurnYaw * TurnYawWeight;

		// Avoid applying curve delta when curve first becomes relevant again
		if (!bLastUpdateValidCurveValue)
		{
			CurveValue = 0.f;
			LastCurveValue = 0.f;
		}
		bLastUpdateValidCurveValue = true;

		// Don't apply if a direction change occurred (this avoids snapping when changing directions)
		if (FMath::Sign(CurveValue) == FMath::Sign(LastCurveValue))
		{
			// Exceeding 180 degrees results in a snap, so maintain current rotation until the turn animation
			// removes the excessive angle
			const float NewTurnOffset = TurnOffset + (CurveValue - LastCurveValue);
			if (FMath::Abs(NewTurnOffset) <= 180.f)
			{
				if (bLastUpdateValidCurveValue)
				{
					TurnOffset = NewTurnOffset;
				}
			}
		}
	}

	// Clamp the turn in place to the max angle if provided; this prevents the character from under-rotating in
	// relation to the control rotation which can cause the character to insufficiently face the camera in shooters
	const FGameplayTag& TurnModeTag = GetTurnModeTag();
	const FTurnInPlaceAngles* TurnAngles = Params.GetTurnAngles(TurnModeTag);
	if (!TurnAngles)
	{
		UE_LOG(LogTurnInPlace, Warning, TEXT("No TurnAngles found for TurnModeTag: %s"), *TurnModeTag.ToString());
	}

	// Clamp the turn offset to the max angle if provided
	const float MaxTurnAngle = TurnAngles ? TurnAngles->MaxTurnAngle : 0.f;
	if (MaxTurnAngle > 0.f && FMath::Abs(TurnOffset) > MaxTurnAngle)
	{
		TurnOffset = FMath::ClampAngle(TurnOffset, -MaxTurnAngle, MaxTurnAngle);
	}

	// Normalize the turn offset to -180 to 180
	const float ActorTurnRotation = FRotator::NormalizeAxis(DesiredRotation.Yaw - (TurnOffset + CurrentRotation.Yaw));

	// Apply the turn offset to the character
	Character->SetActorRotation(CurrentRotation + FRotator(0.f,  ActorTurnRotation, 0.f));
	
#if !UE_BUILD_SHIPPING
	// Log the turn in place values for debugging if set to verbose
	const FString NetRole = GetNetMode() == NM_Standalone ? TEXT("") : Character->GetLocalRole() == ROLE_Authority ? TEXT("[ Server ]") : TEXT("[ Client ]");
	UE_LOG(LogTurnInPlace, Verbose, TEXT("%s cv %.2f  lcv %.2f  offset %.2f"), *NetRole, CurveValue, LastCurveValue, TurnOffset);
#endif
	
#if UE_ENABLE_DEBUG_DRAWING
	DebugRotation();
#endif
}

void UTurnInPlace::PostTurnInPlace(float LastTurnOffset)
{
	// Compress result and replicate to simulated proxy
	CompressSimulatedTurnOffset(LastTurnOffset);
}

void UTurnInPlace::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	// We only want to handle rotation if we are using FaceRotation() and not PhysicsRotation() based on our movement settings
	if (GetTurnMethod() != ETurnMethod::FaceRotation)
	{
		return;
	}

	// Invalid requirements, exit
	if (!HasValidData())
	{
		TurnOffset = 0.f;
		CurveValue = 0.f;
		return;
	}

	// Cache the current rotation
	const FRotator CurrentRotation = Character->GetActorRotation();

	// If the character is stationary, we can turn in place
	if (IsCharacterStationary())
	{
		TurnInPlace(CurrentRotation, NewControlRotation);
		return;
	}

	// This is ACharacter::FaceRotation(), but with interpolation for when we start moving so it doesn't snap
	if (!Character->GetCharacterMovement()->bOrientRotationToMovement)
	{
		if (Character->bUseControllerRotationPitch || Character->bUseControllerRotationYaw || Character->bUseControllerRotationRoll)
		{
			if (!Character->bUseControllerRotationPitch)
			{
				NewControlRotation.Pitch = CurrentRotation.Pitch;
			}

			if (!Character->bUseControllerRotationYaw)
			{
				NewControlRotation.Yaw = CurrentRotation.Yaw;
			}
			else
			{
				// Interpolate away the rotation because we are moving
				const FTurnInPlaceParams Params = GetParams();
				InterpOutAlpha = FMath::FInterpConstantTo(InterpOutAlpha, 1.f, DeltaTime, Params.MovingInterpOutRate);
				NewControlRotation.Yaw = FQuat::Slerp(CurrentRotation.Quaternion(), NewControlRotation.Quaternion(), InterpOutAlpha).GetNormalized().Rotator().Yaw;
			}

			if (!Character->bUseControllerRotationRoll)
			{
				NewControlRotation.Roll = CurrentRotation.Roll;
			}

#if ENABLE_NAN_DIAGNOSTIC
			if (NewControlRotation.ContainsNaN())
			{
				logOrEnsureNanError(TEXT("APawn::FaceRotation about to apply NaN-containing rotation to actor! New:(%s), Current:(%s)"), *NewControlRotation.ToString(), *CurrentRotation.ToString());
			}
#endif

			Character->SetActorRotation(NewControlRotation);
		}
	}
#if UE_ENABLE_DEBUG_DRAWING
	DebugRotation();
#endif
}

bool UTurnInPlace::PhysicsRotation(UCharacterMovementComponent* CharacterMovement, float DeltaTime,
	bool bRotateToLastInputVector, const FVector& LastInputVector)
{
	// We only want to handle rotation if we are using PhysicsRotation() and not FaceRotation() based on our movement settings
	if (GetTurnMethod() != ETurnMethod::PhysicsRotation)
	{
		return false;
	}
	
	// Invalid requirements, exit
	if (!HasValidData())
	{
		TurnOffset = 0.f;
		CurveValue = 0.f;
		return true;
	}

	// Cache the updated component and current rotation
	USceneComponent* UpdatedComponent = CharacterMovement->UpdatedComponent;
	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("UTurnInPlace::PhysicsRotation(): CurrentRotation"));

	// If the character is stationary, we can turn in place
	if (IsCharacterStationary())
	{
		if (bRotateToLastInputVector && CharacterMovement->bOrientRotationToMovement)
		{
			// Rotate towards the last input vector
			TurnInPlace(CurrentRotation, LastInputVector.Rotation());
		}
		else if (CharacterMovement->bUseControllerDesiredRotation && Character->Controller)
		{
			// Rotate towards the controller's desired rotation
			TurnInPlace(CurrentRotation, Character->Controller->GetDesiredRotation());
		}
		else if (!Character->Controller && CharacterMovement->bRunPhysicsWithNoController && CharacterMovement->bUseControllerDesiredRotation)
		{
			// We have no controller, but we can try to find one
			if (AController* ControllerOwner = Cast<AController>(Character->GetOwner()))
			{
				// Rotate towards the controller's desired rotation
				TurnInPlace(CurrentRotation, ControllerOwner->GetDesiredRotation());
			}
		}
		return true;
	}

#if UE_ENABLE_DEBUG_DRAWING
	DebugRotation();
#endif
	
	// We've started moving, CMC can take over by calling Super::PhysicsRotation()
	TurnOffset = 0.f;  // Cull this when we start moving, it will be recalculated when we stop moving
	return false;
}

FTurnInPlaceAnimGraphData UTurnInPlace::UpdateAnimGraphData() const
{
	FTurnInPlaceAnimGraphData AnimGraphData;
	if (!HasValidData())
	{
		return AnimGraphData;
	}

	// Get the current turn in place anim set & parameters from the animation blueprint
	AnimGraphData.AnimSet = ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceAnimSet(AnimInstance);
	FTurnInPlaceParams Params = AnimGraphData.AnimSet.Params;

	// Determine the enabled state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);

	// Retrieve parameters for the current frame required by the animation graph
	AnimGraphData.TurnOffset = TurnOffset;
	AnimGraphData.bIsTurning = IsTurningInPlace();
	AnimGraphData.StepSize = DetermineStepSize(Params, TurnOffset, AnimGraphData.bTurnRight);
	AnimGraphData.TurnModeTag = GetTurnModeTag();

	// Determine if we have valid turn angles for the current turn mode tag and cache the result
	if (const FTurnInPlaceAngles* TurnAngles = Params.GetTurnAngles(GetTurnModeTag()))
	{
		AnimGraphData.TurnAngles = *TurnAngles;
		AnimGraphData.bHasValidTurnAngles = true;
		AnimGraphData.bWantsToTurn = State != ETurnInPlaceEnabledState::Locked && Params.StepSizes.Num() > 0 &&
			FMath::Abs(TurnOffset) >= TurnAngles->MinTurnAngle;
	}
	else
	{
		AnimGraphData.bHasValidTurnAngles = false;
		UE_LOG(LogTurnInPlace, Warning, TEXT("No TurnAngles found for TurnModeTag: %s"), *AnimGraphData.TurnModeTag.ToString());
	}

	return AnimGraphData;
}

int32 UTurnInPlace::DetermineStepSize(const FTurnInPlaceParams& Params, float Angle, bool& bTurnRight)
{
	// Cache the turn angle and step angle
	const float TurnAngle = Angle;
	const float StepAngle = FMath::Abs(TurnAngle) + Params.SelectOffset;

	// Determine if we are turning right or left
	bTurnRight = TurnAngle > 0.f;

	// No step sizes, return 0
	if (Params.StepSizes.Num() == 0)
	{
		ensureMsgf(false, TEXT("No StepSizes found in TurnInPlaceParams"));
		return 0;
	}

	// Determine the step size based on the select mode
	int32 StepSize = 0;
	switch(Params.SelectMode)
	{
	case ETurnAnimSelectMode::Nearest:
		{
			// Find the animation nearest to the angle
			float Diff = 0.f;
			for (int32 i = 0; i < Params.StepSizes.Num(); i++)
			{
				const int32& TAngle = Params.StepSizes[i];
				const float AngleDiff = FMath::Abs(StepAngle - (float)TAngle);
				if (i == 0 || AngleDiff < Diff)
				{
					Diff = AngleDiff;
					StepSize = i;
				}
			}
		}
		break;
	case ETurnAnimSelectMode::Greater:
		{
			// Find the highest animation that exceeds the angle
			for (int32 i = 0; i < Params.StepSizes.Num(); i++)
			{
				const int32& TAngle = Params.StepSizes[i];
				if (FMath::FloorToInt(StepAngle) >= TAngle)
				{
					StepSize = i;
				}
			}
		}
		break;
	default: ;
	}

	return StepSize;
}

#if UE_ENABLE_DEBUG_DRAWING
static bool bHasWarnedSimpleAnimation = false;
void UTurnInPlace::DebugRotation() const
{
	if (!IsValid(Character))
	{
		return;
	}

	// Turn Offset Screen Text
	if (TurnInPlaceCvars::bDebugTurnOffset && GEngine)
	{
		// Don't overwrite other character's screen messages
		const uint64 DebugKey = Character->GetUniqueID() + 1569;
		const FString CharacterRole = Character->HasAuthority() ? TEXT("Server") : Character->GetLocalRole() == ROLE_AutonomousProxy ? TEXT("Client") : TEXT("Simulated");
		GEngine->AddOnScreenDebugMessage(DebugKey, 1.f, FColor::White, FString::Printf(TEXT("[ %s ] TurnOffset: %.2f"), *CharacterRole, TurnOffset));
	}

	// Draw Debug Arrows
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector& ActorLocation = Character->GetActorLocation();
	const FVector Location = (ActorLocation - (FVector::UpVector * HalfHeight));

	// Actor Rotation Vector
	if (TurnInPlaceCvars::bDebugActorDirectionArrow)
	{
		DrawDebugDirectionalArrow(Character->GetWorld(), Location,
			Location + (Character->GetActorForwardVector() * 200.f), 40.f, FColor(199, 10, 143),
			false, -1, 0, 2.f);
	}

	// Control Rotation Vector
	if (TurnInPlaceCvars::bDebugControlDirectionArrow)
	{
		DrawDebugDirectionalArrow(Character->GetWorld(), Location,
			Location + (FRotator(0.f, Character->GetControlRotation().Yaw, 0.f).Vector() * 200.f), 40.f,
			FColor::Black, false, -1, 0, 2.f);
	}

	// Turn Rotation Vector
	if (TurnInPlaceCvars::bDebugTurnOffsetArrow)
	{
		const FVector TurnVector = (Character->GetActorRotation() + FRotator(0.f, TurnOffset, 0.f)).GetNormalized().Vector();
		DrawDebugDirectionalArrow(Character->GetWorld(), Location, Location + (TurnVector * 200.f),
			40.f, FColor(38, 199, 0), false, -1, 0, 2.f);
	}
}

void UTurnInPlace::DebugServerAnim() const
{
	// Draw Server's physics bodies
	if (bDrawServerAnimation && Character->GetLocalRole() == ROLE_Authority && GetNetMode() != NM_Standalone)
	{
#if WITH_SIMPLE_ANIMATION
		USimpleAnimLib::DrawPawnDebugPhysicsBodies(Character, GetMesh(), true, false, false);
#else
		if (!bHasWarnedSimpleAnimation)
		{
			bHasWarnedSimpleAnimation = true;
			const FText ErrorMsg = FText::Format(
				LOCTEXT("NoSimpleAnimPlugin", "{0} is trying to draw server animation but SimpleAnimation plugin was not found. Disable UTurnInPlace::bDrawServerAnimation"),
				FText::FromString(GetName()));
#if WITH_EDITOR
			// Show a notification in the editor
			FNotificationInfo Info(ErrorMsg);
			Info.ExpireDuration = 6.f;
			FSlateNotificationManager::Get().AddNotification(Info);

			// Log the error to message log
			FMessageLog("PIE").Error(ErrorMsg);
#else
			// Log the error to the output log
			UE_LOG(LogTurnInPlaceCharacter, Error, TEXT("%s"), *ErrorMsg.ToString());
#endif
		}
#endif
	}
}
#endif

#undef LOCTEXT_NAMESPACE