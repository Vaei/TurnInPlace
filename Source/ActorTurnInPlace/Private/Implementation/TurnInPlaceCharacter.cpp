// Copyright (c) Jared Taylor. All Rights Reserved


#include "Implementation/TurnInPlaceCharacter.h"

#include "TurnInPlace.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Implementation/TurnInPlaceMovement.h"

#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#if WITH_EDITOR
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif

#include "TurnInPlaceStatics.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlaceCharacter)

ATurnInPlaceCharacter::ATurnInPlaceCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTurnInPlaceMovement>(CharacterMovementComponentName))
{
	TurnInPlaceMovement = Cast<UTurnInPlaceMovement>(GetCharacterMovement());

	if (GetMesh())
	{
		// Server cannot turn in place with the default option (AlwaysTickPose), so we need to change it
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void ATurnInPlaceCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Push Model
	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;
	SharedParams.Condition = COND_SimulatedOnly;

	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, SimulatedTurnOffset, SharedParams);
}

void ATurnInPlaceCharacter::PreInitializeComponents()
{
	TurnInPlace = FindComponentByClass<UTurnInPlace>();
	if (!TurnInPlace)
	{
		const FText ErrorMsg = FText::Format(
			NSLOCTEXT("TurnInPlaceCharacter", "InvalidTurnComp", "No turn in place component found for {0}. Setup is invalid and turn in place cannot occur."),
			FText::FromString(GetName()));
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

	Super::PreInitializeComponents();
}

void ATurnInPlaceCharacter::OnRep_SimulatedTurnOffset()
{
	if (GetLocalRole() == ROLE_SimulatedProxy && TurnInPlace && TurnInPlace->HasValidData())
	{
		TurnInPlace->TurnOffset = SimulatedTurnOffset.Decompress();
	}
}

bool ATurnInPlaceCharacter::IsTurningInPlace() const
{
	return TurnInPlace && TurnInPlace->IsTurningInPlace();
}

bool ATurnInPlaceCharacter::SetCharacterRotation(const FRotator& NewRotation,
	ETeleportType Teleport, ERotationSweepHandling SweepHandling)
{
	return UTurnInPlaceStatics::SetCharacterRotation(this, NewRotation, Teleport, SweepHandling);
}

bool ATurnInPlaceCharacter::TurnInPlaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	if (TurnInPlace && TurnInPlace->HasValidData())
	{
		// LastInputVector won't set from Velocity following root motion, so we need to set it here
		if (HasAnyRootMotion() && TurnInPlaceMovement)
		{
			TurnInPlaceMovement->LastRootMotionTime = GetWorld()->GetTimeSeconds();
		}

		const float LastTurnOffset = TurnInPlace->TurnOffset;

		// This is where the core logic of the TurnInPlace system is processed
		const FVector ForwardVector = TurnInPlaceMovement ? TurnInPlaceMovement->LastInputVector : GetActorForwardVector();
		TurnInPlace->FaceRotation(NewControlRotation, ForwardVector, DeltaTime);

		// Compress result and replicate to simulated proxy
		if (HasAuthority() && GetNetMode() != NM_Standalone)
		{
			const FQuat LastTurnQuat = FRotator(0.f, LastTurnOffset, 0.f).Quaternion();
			const FQuat CurrentTurnQuat = FRotator(0.f, TurnInPlace->TurnOffset, 0.f).Quaternion();
			if (!CurrentTurnQuat.Equals(LastTurnQuat, TURN_ROTATOR_TOLERANCE))
			{
				SimulatedTurnOffset.Compress(TurnInPlace->TurnOffset);
				MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, SimulatedTurnOffset, this);
			}
		}
		
		return true;
	}
	return false;
}

void ATurnInPlaceCharacter::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	if (!TurnInPlaceRotation(NewControlRotation, DeltaTime))
	{
		SuperFaceRotation(NewControlRotation, DeltaTime);
	}
}

void ATurnInPlaceCharacter::SuperFaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	// Override to use SetCharacterRotation instead of SetActorRotation
	
	// Only if we actually are going to use any component of rotation.
	if (bUseControllerRotationPitch || bUseControllerRotationYaw || bUseControllerRotationRoll)
	{
		const FRotator CurrentRotation = GetActorRotation();

		if (!bUseControllerRotationPitch)
		{
			NewControlRotation.Pitch = CurrentRotation.Pitch;
		}

		if (!bUseControllerRotationYaw)
		{
			NewControlRotation.Yaw = CurrentRotation.Yaw;
		}

		if (!bUseControllerRotationRoll)
		{
			NewControlRotation.Roll = CurrentRotation.Roll;
		}

#if ENABLE_NAN_DIAGNOSTIC
		if (NewControlRotation.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("APawn::FaceRotation about to apply NaN-containing rotation to actor! New:(%s), Current:(%s)"), *NewControlRotation.ToString(), *CurrentRotation.ToString());
		}
#endif

		// Optimized to not sweep if we're only changing Yaw which cannot collide
		SetCharacterRotation(NewControlRotation);
	}
}
