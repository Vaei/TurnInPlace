// Copyright (c) 2025 Jared Taylor


#include "Implementation/TurnInPlaceCharacter.h"

#include "TurnInPlace.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Implementation/TurnInPlaceMovement.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TurnInPlaceStatics.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlaceCharacter)

FName ATurnInPlaceCharacter::TurnInPlaceComponentName(TEXT("TurnInPlace"));

ATurnInPlaceCharacter::ATurnInPlaceCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTurnInPlaceMovement>(CharacterMovementComponentName))
{
	TurnInPlaceMovement = Cast<UTurnInPlaceMovement>(GetCharacterMovement());

	TurnInPlace = CreateOptionalDefaultSubobject<UTurnInPlace>(TurnInPlaceComponentName);

	if (GetMesh())
	{
		/*
		 * Server cannot turn in place with the default option (AlwaysTickPose), so we need to change it.
		 * You may want to experiment with these options for games with large character counts, as it can affect performance
		 *
		 * @NOTE: You can use UTurnInPlace::DedicatedServerAnimUpdateMode::Pseudo to avoid ticking the mesh on the server instead
		 * This will make the server run a pseudo anim state instead of playing actual animations
         */
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
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
		const float LastTurnOffset = TurnInPlace->GetTurnOffset();

		// This is where the core logic of the TurnInPlace system is processed
		const bool bHandled = TurnInPlace->FaceRotation(NewControlRotation, DeltaTime);

		// Replicate the turn offset to simulated proxies
		TurnInPlace->PostTurnInPlace(LastTurnOffset);

		// We handled the rotation
		return bHandled;
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

	// Simulated proxies may need to deduct the turn offset based on animation curves so that they aren't stuck in a
	// turn while awaiting their next replication update if the server ticks at an incredibly low frequency
	if (TurnInPlace)
	{
		TurnInPlace->SimulateTurnInPlace();
	}

#if UE_ENABLE_DEBUG_DRAWING
	// Don't attempt this in FaceRotation() or PhysicsRotation() because it will jitter due to unexpected delta
	// times (e.g. from replication events, from physics sub-ticks, etc.)
	if (TurnInPlace && TurnInPlace->HasValidData())
	{
		TurnInPlace->DebugRotation();
	}
#endif
}
