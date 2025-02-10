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

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlace)

DEFINE_LOG_CATEGORY_STATIC(LogTurnInPlace, Log, All);

namespace TurnInPlaceCvars
{
#if UE_ENABLE_DEBUG_DRAWING
	static bool bDebugNetworkSettings = false;
	FAutoConsoleVariableRef CVarDebugNetworkSettings(
		TEXT("p.Turn.Debug.NetworkSettings"),
		bDebugNetworkSettings,
		TEXT("Print issues with network settings to log"),
		ECVF_Default);

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
	, InitialStartAngle(0)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
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
	Character = IsValid(GetOwner()) ? Cast<ACharacter>(GetOwner()) : nullptr;
}

void UTurnInPlace::BeginPlay()
{
	Super::BeginPlay();

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
	AnimInstance = GetMesh()->GetAnimInstance();
	bIsValidAnimInstance = false;
	if (IsValid(AnimInstance))
	{
		bIsValidAnimInstance = AnimInstance->Implements<UTurnInPlaceAnimInterface>();
		if (!bIsValidAnimInstance && bWarnIfAnimInterfaceNotImplemented && !bHasWarned)
		{
			bHasWarned = true;
			const FText ErrorMsg = FText::Format(
				NSLOCTEXT("TurnInPlaceComponent", "InvalidAnimInstance", "The anim instance {0} assigned to {1} on {2} does not implement the TurnInPlaceAnimInterface."),
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
	if (bIsValidAnimInstance && Character && Character->IsPlayingNetworkedRootMotionMontage())
	{
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
		if (ShouldIgnoreRootMotionMontage(Montage))
		{
			return ETurnInPlaceOverride::ForcePaused;
		}
	}
	
	return ETurnInPlaceOverride::Default;
}

bool UTurnInPlace::IsStrafing_Implementation() const
{
	return Character && Character->GetCharacterMovement() && !Character->GetCharacterMovement()->bOrientRotationToMovement;
}

ETurnInPlaceEnabledState UTurnInPlace::GetEnabledState(const FTurnInPlaceParams& Params) const
{
	if (!HasValidData())
	{
		return ETurnInPlaceEnabledState::Locked;
	}
	
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
	
	FTurnInPlaceAnimSet AnimSet = ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceAnimSet(AnimInstance);
	return AnimSet.Params;
}

FTurnInPlaceCurveValues UTurnInPlace::GetCurveValues() const
{
	if (!HasValidData())
	{
		return {};
	}
	
	return ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceCurveValues(AnimInstance);
}

bool UTurnInPlace::HasValidData() const
{
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
#if UE_ENABLE_DEBUG_DRAWING
	if (TurnInPlaceCvars::bDebugNetworkSettings)
	{
		if (GetNetMode() == NM_DedicatedServer && Character->HasAuthority())
		{
			const float RotationTime = Character->GetCharacterMovement() ? Character->GetCharacterMovement()->NetworkSimulatedSmoothRotationTime : 100.f;
			if (RotationTime < 0.1f)
			{
				UE_LOG(LogTurnInPlace, Warning, TEXT("NetworkSimulatedSmoothRotationTime is { %f }, this will jitter turn in place, recommend value of 0.2. If using a lower value intentionally disable bDebugNetworkSettings CVar"), RotationTime);
			}
		}
		if (GetNetMode() == NM_ListenServer && Character->HasAuthority())
		{
			const float RotationTime = Character->GetCharacterMovement() ? Character->GetCharacterMovement()->ListenServerNetworkSimulatedSmoothRotationTime : 100.f;
			if (RotationTime < 0.05f)
			{
				UE_LOG(LogTurnInPlace, Warning, TEXT("NetworkSimulatedSmoothRotationTime is { %f }, this will jitter turn in place, recommend value of 0.12. If using a lower value intentionally disable bDebugNetworkSettings CVar"), RotationTime);
			}
		}
	}
#endif
	
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
	const float MaxTurnAngle = TurnAngles ? TurnAngles->MaxTurnAngle : 0.f;
	if (MaxTurnAngle > 0.f && FMath::Abs(TurnOffset) > MaxTurnAngle)
	{
		TurnOffset = FMath::ClampAngle(TurnOffset, -MaxTurnAngle, MaxTurnAngle);
	}

	const float ActorTurnRotation = FRotator::NormalizeAxis(DesiredRotation.Yaw - (TurnOffset + CurrentRotation.Yaw));
	Character->SetActorRotation(CurrentRotation + FRotator(0.f,  ActorTurnRotation, 0.f));
	
#if !UE_BUILD_SHIPPING
	const FString NetRole = GetNetMode() == NM_Standalone ? TEXT("") : Character->GetLocalRole() == ROLE_Authority ? TEXT("[ Server ]") : TEXT("[ Client ]");
	UE_LOG(LogTurnInPlace, Verbose, TEXT("%s cv %.2f  lcv %.2f  offset %.2f"), *NetRole, CurveValue, LastCurveValue, TurnOffset);
#endif
	
#if UE_ENABLE_DEBUG_DRAWING
	DebugRotation();
#endif
}

void UTurnInPlace::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
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
	
	const FRotator CurrentRotation = Character->GetActorRotation();
	if (IsCharacterStationary())
	{
		TurnInPlace(CurrentRotation, NewControlRotation);
		return;
	}

	// This is ACharacter::FaceRotation(), but with interpolation for when we start moving so it doesn't snap
#if !UE_BUILD_SHIPPING
	const FTurnInPlaceParams Params = GetParams();
	const EInterpOutMode& InterpOutMode = IsStrafing() ? Params.StrafeInterpOutMode : Params.MovementInterpOutMode;
	switch(InterpOutMode)
	{
	case EInterpOutMode::AnimationCurve:
		{
			// Invalid because we're snapping to our control rotation, so we can't possibly blend into an animation
			// This is only appropriate for PhysicsRotation()
			ensure(false);
		}
		break;
	case EInterpOutMode::Interpolation:
#endif
		{
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
						// Interpolate away the rotation
						const float& InterpOutRate = IsStrafing() ? Params.StrafeInterpOutRate : Params.MovementInterpOutRate;
						InterpOutAlpha = FMath::FInterpConstantTo(InterpOutAlpha, 1.f, DeltaTime, InterpOutRate);
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
		}
#if !UE_BUILD_SHIPPING
		break;
	default: ;
	}
#endif

#if UE_ENABLE_DEBUG_DRAWING
	DebugRotation();
#endif
}

bool UTurnInPlace::PhysicsRotation(UCharacterMovementComponent* CharacterMovement, float DeltaTime,
	bool bRotateToLastInputVector, const FVector& LastInputVector)
{
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
	
	USceneComponent* UpdatedComponent = CharacterMovement->UpdatedComponent;
	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("UTurnInPlace::PhysicsRotation(): CurrentRotation"));

	if (IsCharacterStationary())
	{
		if (bRotateToLastInputVector && CharacterMovement->bOrientRotationToMovement)
		{
			TurnInPlace(CurrentRotation, LastInputVector.Rotation());
		}
		else if (CharacterMovement->bUseControllerDesiredRotation && Character->Controller)
		{
			TurnInPlace(CurrentRotation, Character->Controller->GetDesiredRotation());
		}
		else if (!Character->Controller && CharacterMovement->bRunPhysicsWithNoController && CharacterMovement->bUseControllerDesiredRotation)
		{
			if (AController* ControllerOwner = Cast<AController>(Character->GetOwner()))
			{
				TurnInPlace(CurrentRotation, ControllerOwner->GetDesiredRotation());
			}
		}
		return true;
	}

#if UE_ENABLE_DEBUG_DRAWING
		DebugRotation();
#endif
	
	// We've started moving, CMC can take over
	TurnOffset = 0.f;
	return false;
	
	//
	// // As identical to UCharacterMovementComponent::PhysicsRotation() as possible, but with turn in place support
	//
	// FRotator DeltaRot = CharacterMovement->GetDeltaRotation(DeltaTime);
	// DeltaRot.DiagnosticCheckNaN(TEXT("UTurnInPlace::PhysicsRotation(): GetDeltaRotation"));
	//
	// FRotator DesiredRotation = CurrentRotation;
	// if (CharacterMovement->bOrientRotationToMovement)
	// {
	// 	DesiredRotation = CharacterMovement->ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRot);
	// }
	// else if (Character->Controller && CharacterMovement->bUseControllerDesiredRotation)
	// {
	// 	DesiredRotation = Character->Controller->GetDesiredRotation();
	// }
	// else if (!Character->Controller && CharacterMovement->bRunPhysicsWithNoController && CharacterMovement->bUseControllerDesiredRotation)
	// {
	// 	if (AController* ControllerOwner = Cast<AController>(Character->GetOwner()))
	// 	{
	// 		DesiredRotation = ControllerOwner->GetDesiredRotation();
	// 	}
	// }
	// else
	// {
	// 	return true;
	// }

// 	const bool bWantsToBeVertical = CharacterMovement->ShouldRemainVertical();
// 	
// 	if (bWantsToBeVertical)
// 	{
// 		if (CharacterMovement->HasCustomGravity())
// 		{
// 			FRotator GravityRelativeDesiredRotation = (CharacterMovement->GetGravityToWorldTransform() * DesiredRotation.Quaternion()).Rotator();
// 			GravityRelativeDesiredRotation.Pitch = 0.f;
// 			GravityRelativeDesiredRotation.Yaw = FRotator::NormalizeAxis(GravityRelativeDesiredRotation.Yaw);
// 			GravityRelativeDesiredRotation.Roll = 0.f;
// 			DesiredRotation = (CharacterMovement->GetWorldToGravityTransform() * GravityRelativeDesiredRotation.Quaternion()).Rotator();
// 		}
// 		else
// 		{
// 			DesiredRotation.Pitch = 0.f;
// 			DesiredRotation.Yaw = FRotator::NormalizeAxis(DesiredRotation.Yaw);
// 			DesiredRotation.Roll = 0.f;
// 		}
// 	}
// 	else
// 	{
// 		DesiredRotation.Normalize();
// 	}
// 	
// 	// Accumulate a desired new rotation.
// 	constexpr float AngleTolerance = 1e-3f;
//
// 	if (!CurrentRotation.Equals(DesiredRotation, AngleTolerance))
// 	{
// 		// If we'd be prevented from becoming vertical, override the non-yaw rotation rates to allow the character to snap upright
// 		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.PreventNonVerticalOrientationBlock"));
// 		const int32 bPreventNonVerticalOrientationBlock = CVar ? CVar->GetInt() : 1;
// 		if (bPreventNonVerticalOrientationBlock && bWantsToBeVertical)
// 		{
// 			if (FMath::IsNearlyZero(DeltaRot.Pitch))
// 			{
// 				DeltaRot.Pitch = 360.0;
// 			}
// 			if (FMath::IsNearlyZero(DeltaRot.Roll))
// 			{
// 				DeltaRot.Roll = 360.0;
// 			}
// 		}
//
// 		if (CharacterMovement->HasCustomGravity())
// 		{
// 			FRotator GravityRelativeCurrentRotation = (CharacterMovement->GetGravityToWorldTransform() * CurrentRotation.Quaternion()).Rotator();
// 			FRotator GravityRelativeDesiredRotation = (CharacterMovement->GetGravityToWorldTransform() * DesiredRotation.Quaternion()).Rotator();
//
// 			// PITCH
// 			if (!FMath::IsNearlyEqual(GravityRelativeCurrentRotation.Pitch, GravityRelativeDesiredRotation.Pitch, AngleTolerance))
// 			{
// 				GravityRelativeDesiredRotation.Pitch = FMath::FixedTurn(GravityRelativeCurrentRotation.Pitch, GravityRelativeDesiredRotation.Pitch, DeltaRot.Pitch);
// 			}
//
// 			// YAW
// 			if (!FMath::IsNearlyEqual(GravityRelativeCurrentRotation.Yaw, GravityRelativeDesiredRotation.Yaw, AngleTolerance))
// 			{
// 				GravityRelativeDesiredRotation.Yaw = FMath::FixedTurn(GravityRelativeCurrentRotation.Yaw, GravityRelativeDesiredRotation.Yaw, DeltaRot.Yaw);
// 			}
//
// 			// ROLL
// 			if (!FMath::IsNearlyEqual(GravityRelativeCurrentRotation.Roll, GravityRelativeDesiredRotation.Roll, AngleTolerance))
// 			{
// 				GravityRelativeDesiredRotation.Roll = FMath::FixedTurn(GravityRelativeCurrentRotation.Roll, GravityRelativeDesiredRotation.Roll, DeltaRot.Roll);
// 			}
//
// 			DesiredRotation = (CharacterMovement->GetWorldToGravityTransform() * GravityRelativeDesiredRotation.Quaternion()).Rotator();
// 		}
// 		else
// 		{
// 			// PITCH
// 			if (!FMath::IsNearlyEqual(CurrentRotation.Pitch, DesiredRotation.Pitch, AngleTolerance))
// 			{
// 				DesiredRotation.Pitch = FMath::FixedTurn(CurrentRotation.Pitch, DesiredRotation.Pitch, DeltaRot.Pitch);
// 			}
//
// 			// YAW
// 			if (!FMath::IsNearlyEqual(CurrentRotation.Yaw, DesiredRotation.Yaw, AngleTolerance))
// 			{
// 				DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);
// 			}
//
// 			// ROLL
// 			if (!FMath::IsNearlyEqual(CurrentRotation.Roll, DesiredRotation.Roll, AngleTolerance))
// 			{
// 				DesiredRotation.Roll = FMath::FixedTurn(CurrentRotation.Roll, DesiredRotation.Roll, DeltaRot.Roll);
// 			}
// 		}
// 	}
//
// 	// if (Character->GetVelocity().IsNearlyZero())
// 	// {
// 	// 	TurnInPlace(CurrentRotation, DesiredRotation);
// 	// }
// 	// else
// 	{
// 		// Set the new rotation.
// 		DesiredRotation.DiagnosticCheckNaN(TEXT("TurnInPlace::PhysicsRotation(): DesiredRotation"));
// 		CharacterMovement->MoveUpdatedComponent( FVector::ZeroVector, DesiredRotation, /*bSweep*/ false );
//
// #if UE_ENABLE_DEBUG_DRAWING
// 		DebugRotation();
// #endif
// 	}
//
// 	return true;
}

void UTurnInPlace::OnRootMotionIsPlaying()
{
	bStartRotationInitialized = false;
}

FTurnInPlaceAnimGraphData UTurnInPlace::UpdateAnimGraphData() const
{
	FTurnInPlaceAnimGraphData AnimGraphData;
	if (!HasValidData())
	{
		return AnimGraphData;
	}
	AnimGraphData.AnimSet = ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceAnimSet(AnimInstance);
	FTurnInPlaceParams Params = AnimGraphData.AnimSet.Params;
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);

	AnimGraphData.TurnOffset = TurnOffset;
	AnimGraphData.bIsTurning = IsTurningInPlace();
	AnimGraphData.StepSize = DetermineStepSize(Params, TurnOffset, AnimGraphData.bTurnRight);
	AnimGraphData.bIsStrafing = IsStrafing();
	AnimGraphData.TurnModeTag = GetTurnModeTag();

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
	const float TurnAngle = Angle;
	// const float TurnAngle = FRotator::NormalizeAxis(Angle);
	const float StepAngle = FMath::Abs(TurnAngle) + Params.SelectOffset;
	bTurnRight = TurnAngle > 0.f;

	int32 StepSize = 0;

	if (Params.StepSizes.Num() == 0)
	{
		return StepSize;
	}

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

	// const bool bWants = GetParams().bCanTurnInPlace && TurnInPlaceOverrideState != ETurnEnabledOverrideState::Disabled && GetParams().StepSizes.Num() > 0 && StepAngle >= GetParams().MinTurnAngle;
	// if (bWants)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("return %d for %f angle"), StepSize, StepAngle);
	// }

	return StepSize;
}

#if UE_ENABLE_DEBUG_DRAWING
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
#endif