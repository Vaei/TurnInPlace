// Copyright (c) 2025 Jared Taylor


#include "TurnInPlace.h"

#include "GameplayTagContainer.h"
#include "TurnInPlaceAnimInterface.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#if WITH_EDITOR
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif

#if WITH_SIMPLE_ANIMATION && UE_ENABLE_DEBUG_DRAWING
#include "SimpleAnimLib.h"
#endif

#include "TurnInPlaceStatics.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

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

#if !UE_BUILD_SHIPPING
	static int32 OverrideTurnInPlace = 0;
	FAutoConsoleVariableRef CVarOverrideTurnInPlace(
		TEXT("p.Turn.Override"),
		OverrideTurnInPlace,
		TEXT("Override Turn In Place. 0 = Default, 1 = Force Enabled, 2 = Force Locked, 3 = Force Paused (Disabled)"),
		ECVF_Cheat);
#endif
}

UTurnInPlace::UTurnInPlace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsValidAnimInstance(false)
	, bWarnIfAnimInterfaceNotImplemented(true)
	, bHasWarned(false)
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
	return IsValid(GetOwner()) ? GetOwner()->GetLocalRole() : ROLE_None;
}

bool UTurnInPlace::HasAuthority() const
{
	return IsValid(GetOwner()) ? GetOwner()->HasAuthority() : false;
}

void UTurnInPlace::CompressSimulatedTurnOffset(float LastTurnOffset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::CompressSimulatedTurnOffset);
	
	// Compress result and replicate turn offset to simulated proxy
	if (HasAuthority() && GetNetMode() != NM_Standalone)
	{
		if (HasTurnOffsetChanged(GetTurnOffset(), LastTurnOffset))
		{
			SimulatedTurnOffset.Compress(GetTurnOffset());
			MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, SimulatedTurnOffset, this);
		}
	}
}

void UTurnInPlace::OnRep_SimulatedTurnOffset()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::OnRep_SimulatedTurnOffset);
	
	// Decompress the replicated value from short to float, and apply it to the TurnInPlace component
	// This keeps simulated proxies in sync with the server and allows them to turn in place
	if (GetLocalRole() == ROLE_SimulatedProxy && HasValidData())
	{
		TurnData.TurnOffset = SimulatedTurnOffset.Decompress();
	}
}

void UTurnInPlace::OnRegister()
{
	Super::OnRegister();
	
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		CacheUpdatedCharacter();
	}
}

void UTurnInPlace::InitializeComponent()
{
	Super::InitializeComponent();
	CacheUpdatedCharacter();
}

void UTurnInPlace::CacheUpdatedCharacter_Implementation()
{
	PawnOwner = IsValid(GetOwner()) ? Cast<APawn>(GetOwner()) : nullptr;
	MaybeCharacter = IsValid(GetOwner()) ? Cast<ACharacter>(GetOwner()) : nullptr;
}

void UTurnInPlace::BeginPlay()
{
	Super::BeginPlay();

	// Bind to the Mesh event to detect when the AnimInstance changes so we can recache it and check if it implements UTurnInPlaceAnimInterface
	if (ensureAlways(IsValid(GetOwner())))
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
			UE_LOG(LogTurnInPlace, Error, TEXT("%s"), *ErrorMsg.ToString());
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
	if (MaybeCharacter)
	{
		return MaybeCharacter->GetMesh();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

bool UTurnInPlace::IsCharacterStationary() const
{
	return GetOwner()->GetVelocity().IsNearlyZero();
}

UAnimMontage* UTurnInPlace::GetCurrentMontage() const
{
	if (bIsValidAnimInstance)
	{
		// Get the root motion montage instance and return the montage
		if (const FAnimMontageInstance* MontageInstance = AnimInstance->GetRootMotionMontageInstance())
		{
			return MontageInstance->Montage;
		}
	}
	return nullptr;
}

UAnimMontage* UTurnInPlace::GetCurrentNetworkRootMotionMontage() const
{
	// Check if the character is playing a networked root motion montage
	if (bIsValidAnimInstance && IsPlayingNetworkedRootMotionMontage())
	{
		// Get the root motion montage instance and return the montage
		if (const FAnimMontageInstance* MontageInstance = AnimInstance->GetRootMotionMontageInstance())
		{
			return MontageInstance->Montage;
		}
	}
	return nullptr;
}

ETurnInPlaceOverride UTurnInPlace::GetOverrideForMontage_Implementation(const UAnimMontage* Montage) const
{
	// Allow overriding per-montage
	if (HasValidData() && Montage)
	{
		FTurnInPlaceParams Params = GetParams();
		if (const ETurnInPlaceOverride* Override = Params.MontageHandling.MontageOverrides.Find(Montage))
		{
#if WITH_EDITOR
			if (*Override == ETurnInPlaceOverride::Default)
			{
				FMessageLog("PIE").Warning(FText::Format(
					LOCTEXT("MontageOverrideDefault", "Montage {0} has an override of Default. AnimInstance {1}. Owner {2}. This will be ignored."),
					FText::FromString(Montage->GetName()), FText::FromString(AnimInstance->GetName()), FText::FromString(GetOwner()->GetName())));
			}
#endif
			return *Override;
		}
	}
	return ETurnInPlaceOverride::Default;
}

AController* UTurnInPlace::GetController_Implementation() const
{
	return IsValid(MaybeCharacter) ? MaybeCharacter->GetController() : nullptr;
}

bool UTurnInPlace::IsPlayingNetworkedRootMotionMontage_Implementation() const
{
	return IsValid(MaybeCharacter) && MaybeCharacter->IsPlayingNetworkedRootMotionMontage();
}

bool UTurnInPlace::ShouldIgnoreRootMotionMontage_Implementation(const UAnimMontage* Montage) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::ShouldIgnoreRootMotionMontage);
	
	if (!HasValidData())
	{
		return false;
	}

	FTurnInPlaceParams Params = GetParams();

	// Check if the montage itself is ignored
	if (Params.MontageHandling.IgnoreMontages.Contains(Montage))
	{
		return true;
	}

	// We generally don't want to consider any montages that are additive as playing a montage
	if (Params.MontageHandling.bIgnoreAdditiveMontages && Montage->IsValidAdditive())
	{
		return true;
	}

	// Check if any montage anim tracks ignore this slot
	for (const FName& Slot : Params.MontageHandling.IgnoreMontageSlots)
	{
		if (Montage->IsValidSlot(Slot))
		{
			return true;
		}
	}
	
	return false;
}

FVector UTurnInPlace::GetDebugDrawArrowLocation_Implementation(bool& bIsValidLocation) const
{
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCapsuleComponent())
	{
		bIsValidLocation = false;
		return FVector::ZeroVector;
	}

	bIsValidLocation = true;
	
	const float HalfHeight = MaybeCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector& ActorLocation = MaybeCharacter->GetActorLocation();
	return ActorLocation - (FVector::UpVector * HalfHeight);
}

ETurnInPlaceOverride UTurnInPlace::OverrideTurnInPlace_Implementation() const
{
#if !UE_BUILD_SHIPPING
	if (TurnInPlaceCvars::OverrideTurnInPlace > 0)
	{
		switch (TurnInPlaceCvars::OverrideTurnInPlace)
		{
		case 1: return ETurnInPlaceOverride::ForceEnabled;
		case 2: return ETurnInPlaceOverride::ForceLocked;
		case 3: return ETurnInPlaceOverride::ForcePaused;
		default: break;
		}
	}
#endif

	// Curve values are used to determine if we should pause or lock turn in place
	const FTurnInPlaceCurveValues& CurveValues = GetCurveValues();

	if (FMath::IsNearlyEqual(CurveValues.PauseTurnInPlace, 1.f, 0.05f))
	{
		// We want to pause turn in place if the weight curve is not 0
		return ETurnInPlaceOverride::ForcePaused;
	}

	if (FMath::IsNearlyEqual(CurveValues.LockTurnInPlace, 1.f, 0.05f))
	{
		// We want to lock turn in place if the weight curve is not 0
		return ETurnInPlaceOverride::ForceLocked;
	}
	
	// Allow specific override per-montage
	if (const UAnimMontage* Montage = GetCurrentMontage())
	{
		// But we don't want to pause turn in place if the montage is ignored by our current params
		const ETurnInPlaceOverride MontageOverride = GetOverrideForMontage(Montage);
		if (MontageOverride != ETurnInPlaceOverride::Default)
		{
			return MontageOverride;
		}
	}
	
	// We want to pause turn in place when using root motion montages
	if (const UAnimMontage* Montage = GetCurrentNetworkRootMotionMontage())
	{
		// But we don't want to pause turn in place if the montage is ignored by our current params
		if (!ShouldIgnoreRootMotionMontage(Montage))
		{
			return ETurnInPlaceOverride::ForcePaused;
		}
	}

	return ETurnInPlaceOverride::Default;
}

FGameplayTag UTurnInPlace::GetTurnModeTag_Implementation() const
{
	// Determine the turn mode tag based on the character's movement settings
	const bool bIsStrafing = MaybeCharacter && MaybeCharacter->GetCharacterMovement() && !MaybeCharacter->GetCharacterMovement()->bOrientRotationToMovement;
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
	const ETurnInPlaceEnabledState State = Params.State;
	const ETurnInPlaceOverride OverrideState = OverrideTurnInPlace();
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

	// Dedicated server might want to use pseudo anim state instead of playing actual animations
	if (WantsPseudoAnimState())
	{
		if (PseudoAnim)
		{
			const float Yaw = PseudoAnim->EvaluateCurveData(Settings.TurnYawCurveName, PseudoNodeData.AnimStateTime);
			const float Weight = PseudoAnim->EvaluateCurveData(Settings.TurnWeightCurveName, PseudoNodeData.AnimStateTime);
			const float Pause = PseudoAnim->EvaluateCurveData(Settings.PauseTurnInPlaceCurveName, PseudoNodeData.AnimStateTime);
			const float Lock = PseudoAnim->EvaluateCurveData(Settings.LockTurnInPlaceCurveName, PseudoNodeData.AnimStateTime);
			return { Yaw, Weight, Pause, Lock };
		}
	}

	// Get the current turn in place curve values from the animation blueprint
	return ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceCurveValues(AnimInstance);
}

bool UTurnInPlace::WantsPseudoAnimState() const
{
	return GetNetMode() == NM_DedicatedServer && DedicatedServerAnimUpdateMode == ETurnAnimUpdateMode::Pseudo;
}

bool UTurnInPlace::HasValidData() const
{
	// We need a valid AnimInstance and Character to proceed, and the anim instance must implement the TurnInPlaceAnimInterface
	return bIsValidAnimInstance && IsValid(GetOwner()) && !GetOwner()->IsPendingKillPending();
}

ETurnMethod UTurnInPlace::GetTurnMethod() const
{
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCharacterMovement())
	{
		return ETurnMethod::None;
	}

	// ACharacter::FaceRotation handles turn in place when bOrientRotationToMovement is false, and we orient to control rotation
	// This is an instant snapping turn that rotates to control rotation
	if (!MaybeCharacter->GetCharacterMovement()->bOrientRotationToMovement)
	{
		if (MaybeCharacter->bUseControllerRotationPitch || MaybeCharacter->bUseControllerRotationYaw || MaybeCharacter->bUseControllerRotationRoll)
		{
			return ETurnMethod::FaceRotation;
		}
	}

	// UCharacterMovementComponent::PhysicsRotation handles orienting rotation to movement or controller desired rotation
	// This is a smooth rotation that interpolates to the desired rotation
	return ETurnMethod::PhysicsRotation;
}

bool UTurnInPlace::HasTurnOffsetChanged(float CurrentValue, float LastValue)
{
	const FQuat LastTurnQuat = FRotator(0.f, LastValue, 0.f).Quaternion();
	const FQuat CurrentTurnQuat = FRotator(0.f, CurrentValue, 0.f).Quaternion();
	return !CurrentTurnQuat.Equals(LastTurnQuat, TURN_ROTATOR_TOLERANCE);
}

void UTurnInPlace::SimulateTurnInPlace()
{
	if (bSimulateAnimationCurves && HasValidData() && GetOwnerRole() == ROLE_SimulatedProxy && IsCharacterStationary())
	{
		TurnInPlace(FRotator::ZeroRotator, FRotator::ZeroRotator, true);
	}
}

void UTurnInPlace::TurnInPlace(const FRotator& CurrentRotation, const FRotator& DesiredRotation, bool bClientSimulation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::TurnInPlace);

	// Determine the correct params to use
	const FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);
	
	// Turn in place is locked, we can't do anything
	const bool bEnabled = State != ETurnInPlaceEnabledState::Locked;
	if (!bEnabled)
	{
		TurnData = {};
		return;
	}

	if (!bClientSimulation)
	{
		// Reset it here, because we are not appending, and this accounts for velocity being applied (no turn in place)
		TurnData.TurnOffset = 0.f;
		TurnData.InterpOutAlpha = 0.f;

		// If turn in place is paused, we can't accumulate any turn offset
		if (State != ETurnInPlaceEnabledState::Paused)
		{
			TurnData.TurnOffset = (DesiredRotation - CurrentRotation).GetNormalized().Yaw;
		}
	}
	
	// Apply any turning from the animation sequence
	float LastCurveValue = TurnData.CurveValue;
	const FTurnInPlaceCurveValues CurveValues = GetCurveValues();
	const float TurnYawWeight = CurveValues.TurnYawWeight;

	if (FMath::IsNearlyZero(TurnYawWeight, KINDA_SMALL_NUMBER))
	{
		// No curve weight, don't apply any animation yaw
		TurnData.CurveValue = 0.f;
		TurnData.bLastUpdateValidCurveValue = false;
	}
	else
	{
		// Apply the remaining yaw from the current animation (curve) that is playing, scaled by the weight curve
		const float RemainingTurnYaw = CurveValues.RemainingTurnYaw;
		TurnData.CurveValue = RemainingTurnYaw * TurnYawWeight;

		// Avoid applying curve delta when curve first becomes relevant again
		if (!TurnData.bLastUpdateValidCurveValue)
		{
			TurnData.CurveValue = 0.f;
			LastCurveValue = 0.f;
		}
		TurnData.bLastUpdateValidCurveValue = true;

		// Don't apply if a direction change occurred (this avoids snapping when changing directions)
		if (FMath::Sign(TurnData.CurveValue) == FMath::Sign(LastCurveValue))
		{
			// Exceeding 180 degrees results in a snap, so maintain current rotation until the turn animation
			// removes the excessive angle
			const float NewTurnOffset = TurnData.TurnOffset + (TurnData.CurveValue - LastCurveValue);
			if (FMath::Abs(NewTurnOffset) <= 180.f)
			{
				if (TurnData.bLastUpdateValidCurveValue)
				{
					TurnData.TurnOffset = NewTurnOffset;
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
	if (MaxTurnAngle > 0.f && FMath::Abs(TurnData.TurnOffset) > MaxTurnAngle)
	{
		TurnData.TurnOffset = FMath::ClampAngle(TurnData.TurnOffset, -MaxTurnAngle, MaxTurnAngle);
	}

	if (!bClientSimulation)
	{
		// Normalize the turn offset to -180 to 180
		const float ActorTurnRotation = FRotator::NormalizeAxis(DesiredRotation.Yaw - (TurnData.TurnOffset + CurrentRotation.Yaw));

		// Apply the turn offset to the character
		GetOwner()->SetActorRotation(CurrentRotation + FRotator(0.f,  ActorTurnRotation, 0.f));
	}
	
#if !UE_BUILD_SHIPPING
	// Log the turn in place values for debugging if set to verbose
	const FString NetRole = GetNetMode() == NM_Standalone ? TEXT("") : GetOwner()->GetLocalRole() == ROLE_Authority ? TEXT("[ Server ]") : TEXT("[ Client ]");
	UE_LOG(LogTurnInPlace, Verbose, TEXT("%s cv %.2f  lcv %.2f  offset %.2f"), *NetRole, TurnData.CurveValue, LastCurveValue, TurnData.TurnOffset);
#endif
}

void UTurnInPlace::PostTurnInPlace(float LastTurnOffset)
{
	// Compress result and replicate to simulated proxy
	CompressSimulatedTurnOffset(LastTurnOffset);
}

bool UTurnInPlace::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::FaceRotation);
	
	// We only want to handle rotation if we are using FaceRotation() and not PhysicsRotation() based on our movement settings
	if (GetTurnMethod() != ETurnMethod::FaceRotation)
	{
		return true;
	}

	// Invalid requirements, exit
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCharacterMovement())
	{
		TurnData = {};
		return true;
	}
	
	// Determine the correct params to use
	const FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);
	
	// Turn in place is locked, we can't do anything
	const bool bEnabled = State != ETurnInPlaceEnabledState::Paused;
	if (!bEnabled)
	{
		TurnData = {};
		return false;
	}

	// Cache the current rotation
	const FRotator CurrentRotation = GetOwner()->GetActorRotation();

	// If the character is stationary, we can turn in place
	if (IsCharacterStationary())
	{
		TurnInPlace(CurrentRotation, NewControlRotation);
		return true;
	}
	
	TurnData.TurnOffset = 0.f;

	// This is ACharacter::FaceRotation(), but with interpolation for when we start moving so it doesn't snap
	if (!MaybeCharacter->GetCharacterMovement()->bOrientRotationToMovement)
	{
		if (MaybeCharacter->bUseControllerRotationPitch || MaybeCharacter->bUseControllerRotationYaw || MaybeCharacter->bUseControllerRotationRoll)
		{
			if (!MaybeCharacter->bUseControllerRotationPitch)
			{
				NewControlRotation.Pitch = CurrentRotation.Pitch;
			}

			if (!MaybeCharacter->bUseControllerRotationYaw)
			{
				NewControlRotation.Yaw = CurrentRotation.Yaw;
			}
			else
			{
				// Interpolate away the rotation because we are moving
				TurnData.InterpOutAlpha = FMath::FInterpConstantTo(TurnData.InterpOutAlpha, 1.f, DeltaTime, Params.MovingInterpOutRate);
				NewControlRotation.Yaw = FQuat::Slerp(CurrentRotation.Quaternion(), NewControlRotation.Quaternion(), TurnData.InterpOutAlpha).GetNormalized().Rotator().Yaw;
			}

			if (!MaybeCharacter->bUseControllerRotationRoll)
			{
				NewControlRotation.Roll = CurrentRotation.Roll;
			}

#if ENABLE_NAN_DIAGNOSTIC
			if (NewControlRotation.ContainsNaN())
			{
				logOrEnsureNanError(TEXT("APawn::FaceRotation about to apply NaN-containing rotation to actor! New:(%s), Current:(%s)"), *NewControlRotation.ToString(), *CurrentRotation.ToString());
			}
#endif

			GetOwner()->SetActorRotation(NewControlRotation);
		}
	}
	return true;
}

bool UTurnInPlace::PhysicsRotation(UCharacterMovementComponent* CharacterMovement, float DeltaTime,
	bool bRotateToLastInputVector, const FVector& LastInputVector)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::PhysicsRotation);

	// We only want to handle rotation if we are using PhysicsRotation() and not FaceRotation() based on our movement settings
	if (GetTurnMethod() != ETurnMethod::PhysicsRotation)
	{
		return false;
	}
	
	// Invalid requirements, exit
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCharacterMovement())
	{
		TurnData = {};
		return true;
	}

	// Determine the correct params to use
	const FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);
	
	// Turn in place is locked, we can't do anything
	const bool bEnabled = State != ETurnInPlaceEnabledState::Paused;
	if (!bEnabled)
	{
		TurnData = {};
		return false;
	}

	// Cache the updated component and current rotation
	const USceneComponent* UpdatedComponent = CharacterMovement->UpdatedComponent;
	const FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("UTurnInPlace::PhysicsRotation(): CurrentRotation"));

	// If the character is stationary, we can turn in place
	if (IsCharacterStationary())
	{
		if (bRotateToLastInputVector && CharacterMovement->bOrientRotationToMovement)
		{
			// Rotate towards the last input vector
			TurnInPlace(CurrentRotation, LastInputVector.Rotation());
		}
		else if (CharacterMovement->bUseControllerDesiredRotation && MaybeCharacter->Controller)
		{
			// Rotate towards the controller's desired rotation
			TurnInPlace(CurrentRotation, MaybeCharacter->Controller->GetDesiredRotation());
		}
		else if (!MaybeCharacter->Controller && CharacterMovement->bRunPhysicsWithNoController && CharacterMovement->bUseControllerDesiredRotation)
		{
			// We have no controller, but we can try to find one
			if (const AController* ControllerOwner = Cast<AController>(MaybeCharacter->GetOwner()))
			{
				// Rotate towards the controller's desired rotation
				TurnInPlace(CurrentRotation, ControllerOwner->GetDesiredRotation());
			}
		}
		return true;
	}

	// We've started moving, CMC can take over by calling Super::PhysicsRotation()
	TurnData = {};  // Cull turn offset when we start moving, it will be recalculated when we stop moving
	return false;
}

FTurnInPlaceAnimGraphData UTurnInPlace::UpdateAnimGraphData(float DeltaTime) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::UpdateAnimGraphData);
	
	FTurnInPlaceAnimGraphData AnimGraphData;
	if (!HasValidData())
	{
		return AnimGraphData;
	}

	// Get the current turn in place anim set & parameters from the animation blueprint
	AnimGraphData.AnimSet = ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceAnimSet(AnimInstance);
	const FTurnInPlaceParams Params = AnimGraphData.AnimSet.Params;

	// Determine the enabled state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);

	// Retrieve parameters for the current frame required by the animation graph
	const float& TurnOffset = GetTurnOffset();
	AnimGraphData.TurnOffset = TurnOffset;
	AnimGraphData.bIsTurning = IsTurningInPlace();
	AnimGraphData.StepSize = DetermineStepSize(Params, TurnOffset, AnimGraphData.bTurnRight);
	AnimGraphData.TurnModeTag = GetTurnModeTag();
	AnimGraphData.bWantsPseudoAnimState = WantsPseudoAnimState();

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

void UTurnInPlace::PostUpdateAnimGraphData(float DeltaTime, FTurnInPlaceAnimGraphData& AnimGraphData, FTurnInPlaceAnimGraphOutput& TurnOutput)
{
	// Note: We only have valid TurnOutput here if we are updating the pseudo anim state (i.e. dedicated server only!)
	UpdatePseudoAnimState(DeltaTime, AnimGraphData, TurnOutput);
}

void UTurnInPlace::UpdatePseudoAnimState(float DeltaTime, const FTurnInPlaceAnimGraphData& TurnAnimData,
	FTurnInPlaceAnimGraphOutput& TurnOutput)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::ThreadSafeUpdateTurnInPlace);

	// Dedicated server might want to use pseudo anim state instead of playing actual animations
	if (!WantsPseudoAnimState())
	{
		return;
	}

	if (!HasValidData())
	{
		return;
	}

	// Update pseudo state on dedicated server
	const FTurnInPlaceAnimSet& AnimSet = TurnAnimData.AnimSet;

	switch (PseudoAnimState)
	{
	case ETurnPseudoAnimState::Idle:
		if (TurnOutput.bWantsToTurn)
		{
			PseudoAnimState = ETurnPseudoAnimState::TurnInPlace;

			// SetupTurnAnim()
			PseudoNodeData.StepSize = TurnAnimData.StepSize;
			PseudoNodeData.bIsTurningRight = TurnAnimData.bTurnRight;

			// SetupTurnInPlace()
			PseudoNodeData.AnimStateTime = 0.f;
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation(AnimSet, PseudoNodeData, false);
			PseudoNodeData.bHasReachedMaxTurnAngle = false;
			UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceNode(PseudoNodeData, TurnAnimData, AnimSet);
		}
		break;
	case ETurnPseudoAnimState::TurnInPlace:
		if (TurnOutput.bWantsTurnRecovery)
		{
			PseudoAnimState = ETurnPseudoAnimState::Recovery;

			// SetupTurnRecovery() -- AnimStateTime is already carried over from TurnInPlace
			PseudoNodeData.bIsRecoveryTurningRight = PseudoNodeData.bIsTurningRight;
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation(AnimSet, PseudoNodeData, true);
		}
		else
		{
			// UpdateTurnInPlace()
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation(AnimSet, PseudoNodeData, false);
			PseudoNodeData.AnimStateTime = UTurnInPlaceStatics::GetUpdatedTurnInPlaceAnimTime_ThreadSafe(PseudoAnim,
				PseudoNodeData.AnimStateTime, DeltaTime, PseudoNodeData.TurnPlayRate);
			UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceNode(PseudoNodeData, TurnAnimData, AnimSet);
		}
		break;
	case ETurnPseudoAnimState::Recovery:
		{
			// UpdateTurnInPlaceRecovery()
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation(AnimSet, PseudoNodeData, true);
			PseudoNodeData.AnimStateTime = UTurnInPlaceStatics::GetUpdatedTurnInPlaceAnimTime_ThreadSafe(PseudoAnim,
				PseudoNodeData.AnimStateTime, DeltaTime, 1.f);  // Recovery plays at 1x speed
			if (!PseudoAnim || (PseudoAnim && PseudoNodeData.AnimStateTime >= PseudoAnim->GetPlayLength()))
			{
				PseudoAnimState = ETurnPseudoAnimState::Idle;

				// SetupIdle()
				PseudoNodeData.TurnPlayRate = 1.f;
				PseudoNodeData.bHasReachedMaxTurnAngle = false;
			}
		}
		break;
	}
}

int32 UTurnInPlace::DetermineStepSize(const FTurnInPlaceParams& Params, float Angle, bool& bTurnRight)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::DetermineStepSize);
	
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

void UTurnInPlace::DebugRotation() const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!IsValid(GetOwner()))
	{
		return;
	}

	// Optionally draw server's physics bodies so we can visualize what they're doing animation-wise
	DebugServerPhysicsBodies();

	// Turn Offset Screen Text
	if (TurnInPlaceCvars::bDebugTurnOffset && GEngine)
	{
		// Don't overwrite other character's screen messages
		const uint64 DebugKey = GetOwner()->GetUniqueID() + 1569;
		const FRandomStream ColorStream(DebugKey);
		const FColor DebugColor = FColor(ColorStream.RandRange(0, 255), ColorStream.RandRange(0, 255), ColorStream.RandRange(0, 255));
		const FString CharacterRole = GetOwner()->HasAuthority() ? TEXT("Server") : GetOwner()->GetLocalRole() == ROLE_AutonomousProxy ? TEXT("Client") : TEXT("Simulated");
		GEngine->AddOnScreenDebugMessage(DebugKey, 0.5f, DebugColor, FString::Printf(TEXT("[ %s ] TurnOffset: %.2f"), *CharacterRole, GetTurnOffset()));
	}

	// We only want each character on screen to draw this once, so exclude servers from drawing this for the autonomous proxy
	if (GetOwner()->GetRemoteRole() != ROLE_AutonomousProxy)
	{
		// Draw Debug Arrows
		bool bValidLocation = false;
		const FVector Location = GetDebugDrawArrowLocation(bValidLocation);
		if (!bValidLocation)
		{
			return;
		}

		// Actor Rotation Vector
		if (TurnInPlaceCvars::bDebugActorDirectionArrow)
		{
			DrawDebugDirectionalArrow(GetOwner()->GetWorld(), Location,
				Location + (GetOwner()->GetActorForwardVector() * 200.f), 40.f, FColor(199, 10, 143),
				false, -1, 0, 2.f);
		}

		// Control Rotation Vector
		if (TurnInPlaceCvars::bDebugControlDirectionArrow && GetController())
		{
			DrawDebugDirectionalArrow(GetOwner()->GetWorld(), Location,
				Location + (FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f).Vector() * 200.f), 40.f,
				FColor::Black, false, -1, 0, 2.f);
		}

		// Turn Rotation Vector
		if (TurnInPlaceCvars::bDebugTurnOffsetArrow)
		{
			const FVector TurnVector = (GetOwner()->GetActorRotation() + FRotator(0.f, GetTurnOffset(), 0.f)).GetNormalized().Vector();
			DrawDebugDirectionalArrow(GetOwner()->GetWorld(), Location, Location + (TurnVector * 200.f),
				40.f, FColor(38, 199, 0), false, -1, 0, 2.f);
		}
	}
#endif
}

static bool bHasWarnedSimpleAnimation = false;
void UTurnInPlace::DebugServerPhysicsBodies() const
{
#if UE_ENABLE_DEBUG_DRAWING
	// Draw Server's physics bodies
	if (bDrawServerPhysicsBodies && PawnOwner && GetOwner()->GetLocalRole() == ROLE_Authority && GetNetMode() != NM_Standalone)
	{
#if WITH_SIMPLE_ANIMATION
		USimpleAnimLib::DrawPawnDebugPhysicsBodies(PawnOwner, GetMesh(), true, false, false);
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
			UE_LOG(LogTurnInPlace, Error, TEXT("%s"), *ErrorMsg.ToString());
#endif
		}
#endif
	}
#endif
}

#undef LOCTEXT_NAMESPACE