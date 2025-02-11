// Copyright (c) Jared Taylor. All Rights Reserved


#include "Implementation/TurnInPlaceCharacter.h"

#include "TurnInPlace.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Implementation/TurnInPlaceMovement.h"

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
		// You may want to experiment with these options for games with large character counts, as it can affect performance
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void ATurnInPlaceCharacter::PreInitializeComponents()
{
	// Find the TurnInPlace component added to the character, typically in Blueprint
	TurnInPlace = FindComponentByClass<UTurnInPlace>();
	if (!TurnInPlace)
	{
		// Log an error if the component is not found
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

bool ATurnInPlaceCharacter::IsTurningInPlace() const
{
	return TurnInPlace && TurnInPlace->IsTurningInPlace();
}

bool ATurnInPlaceCharacter::TurnInPlaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	// Allow the turn in place system to handle rotation if desired
	if (TurnInPlace && TurnInPlace->HasValidData())
	{
		// LastInputVector won't set from Velocity following root motion, so we need to set it here
		if (HasAnyRootMotion() && TurnInPlaceMovement)
		{
			TurnInPlaceMovement->LastRootMotionTime = GetWorld()->GetTimeSeconds();
		}

		// Cache the last turn offset for replication comparison
		const float LastTurnOffset = TurnInPlace->TurnOffset;

		// This is where the core logic of the TurnInPlace system is processed
		TurnInPlace->FaceRotation(NewControlRotation, DeltaTime);

		// Replicate the turn offset to simulated proxies
		TurnInPlace->PostTurnInPlace(LastTurnOffset);

		// We handled the rotation
		return true;
	}

	// We did not handle rotation
	return false;
}

void ATurnInPlaceCharacter::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	// Allow the turn in place system to handle rotation if desired
	if (!TurnInPlaceRotation(NewControlRotation, DeltaTime))
	{
		// Turn in place system did not handle rotation, so we'll handle it here
		Super::FaceRotation(NewControlRotation, DeltaTime);
	}
}

void ATurnInPlaceCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if UE_ENABLE_DEBUG_DRAWING
	// Don't attempt this in FaceRotation() or PhysicsRotation() because it will jitter due to unexpected delta
	// times (e.g. from replication events, from physics sub-ticks, etc.)
	if (TurnInPlace && TurnInPlace->HasValidData())
	{
		TurnInPlace->DebugServerAnim();
	}
#endif
}
