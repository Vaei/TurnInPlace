// Copyright (c) Jared Taylor. All Rights Reserved


#include "Implementation/TurnInPlaceMovement.h"

#include "TurnInPlace.h"
#include "GameFramework/Character.h"
#include "Implementation/TurnInPlaceCharacter.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlaceMovement)


void UTurnInPlaceMovement::PostLoad()
{
	Super::PostLoad();

	TurnCharacterOwner = Cast<ATurnInPlaceCharacter>(PawnOwner);
}

void UTurnInPlaceMovement::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	Super::SetUpdatedComponent(NewUpdatedComponent);

	TurnCharacterOwner = Cast<ATurnInPlaceCharacter>(PawnOwner);
}

UTurnInPlace* UTurnInPlaceMovement::GetTurnInPlace() const
{
	return TurnCharacterOwner && TurnCharacterOwner->TurnInPlace && TurnCharacterOwner->TurnInPlace->HasValidData() ?
		TurnCharacterOwner->TurnInPlace : nullptr;
}

void UTurnInPlaceMovement::UpdateLastInputVector()
{
	if (bOrientRotationToMovement)
	{
		if (CharacterOwner->HasAnyRootMotion() || CharacterOwner->GetCurrentMontage() != nullptr)
		{
			// Set to component forward during root motion
			LastInputVector = UpdatedComponent->GetForwardVector();
		}
		else
		{
			// Set input vector - additional logic required to prevent gamepad thumbstick from bouncing back past the center
			// line resulting in the character flipping - known mechanical fault with xbox one elite controller
			const bool bSetInputFromAcceleration = !FMath::IsNearlyZero(ComputeAnalogInputModifier(), 0.5f);
			const bool bSetInputFromVelocity = !Velocity.IsNearlyZero(GetMaxSpeed() * 0.05f) && GetWorld()->GetTimeSeconds() >= LastRootMotionTime + 0.25f;
			if (bSetInputFromAcceleration)
			{
				LastInputVector = Acceleration.GetSafeNormal();
			}
			else if (bSetInputFromVelocity)
			{
				LastInputVector = Velocity.GetSafeNormal();
			}
			else if (CharacterOwner->IsBotControlled())
			{
				LastInputVector = CharacterOwner->GetControlRotation().Vector();
			}
		}
	}
	else
	{
		LastInputVector = UpdatedComponent->GetForwardVector();
	}
}

void UTurnInPlaceMovement::ApplyRootMotionToVelocity(float DeltaTime)
{
	// CalcVelocity is bypassed when using root motion, so we need to update it here instead
	UpdateLastInputVector();

	Super::ApplyRootMotionToVelocity(DeltaTime);
}

FRotator UTurnInPlaceMovement::GetRotationRate() const
{
	if (IsMovingOnGround() && Velocity.IsNearlyZero())
	{
		return RotationRateIdle;
	}

	return RotationRate;
}

float GetTurnAxisDeltaRotation(float InAxisRotationRate, float DeltaTime)
{
	// Values over 360 don't do anything, see FMath::FixedTurn. However, we are trying to avoid giant floats from overflowing other calculations.
	return (InAxisRotationRate >= 0.f) ? FMath::Min(InAxisRotationRate * DeltaTime, 360.f) : 360.f;
}

FRotator UTurnInPlaceMovement::GetDeltaRotation(float DeltaTime) const
{
	const FRotator RotateRate = GetRotationRate();
	return FRotator(GetTurnAxisDeltaRotation(RotateRate.Pitch, DeltaTime), GetTurnAxisDeltaRotation(RotateRate.Yaw, DeltaTime), GetTurnAxisDeltaRotation(RotateRate.Roll, DeltaTime));
}

FRotator UTurnInPlaceMovement::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime,
	FRotator& DeltaRotation) const
{
	// If we're not moving, we can turn towards the last input vector instead
	if (Acceleration.SizeSquared() < UE_KINDA_SMALL_NUMBER)
	{
		// AI path following request can orient us in that direction (it's effectively an acceleration)
		if (bHasRequestedVelocity && RequestedVelocity.SizeSquared() > UE_KINDA_SMALL_NUMBER)
		{
			return RequestedVelocity.GetSafeNormal().Rotation();
		}

		// Rotate towards last input vector
		if (bRotateToLastInputVector && !LastInputVector.IsNearlyZero())
		{
			return LastInputVector.Rotation();
		}

		// Don't change rotation if there is no acceleration.
		return CurrentRotation;
	}

	// Rotate toward direction of acceleration.
	return Acceleration.GetSafeNormal().Rotation();
}

void UTurnInPlaceMovement::PhysicsRotation(float DeltaTime)
{
	// We have to repeat the logic from UCharacterMovementComponent::PhysicsRotation() here to allow for the turn in place to work
	// Mostly we do the same, but we route to UTurnInPlace::PhysicsRotation() where/when necessary
	
	if (!(bOrientRotationToMovement || bUseControllerDesiredRotation))
	{
		return;
	}

	if (!HasValidData() || (!CharacterOwner->Controller && !bRunPhysicsWithNoController))
	{
		return;
	}

	if (UTurnInPlace* TurnInPlace = GetTurnInPlace())
	{
		if (!TurnInPlace->PhysicsRotation(this, DeltaTime, bRotateToLastInputVector, LastInputVector))
		{
			Super::PhysicsRotation(DeltaTime);
		}
	}
	else
	{
		Super::PhysicsRotation(DeltaTime);
	}
}
