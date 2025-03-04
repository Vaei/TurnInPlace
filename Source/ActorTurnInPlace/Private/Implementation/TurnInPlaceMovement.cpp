// Copyright (c) 2025 Jared Taylor


#include "Implementation/TurnInPlaceMovement.h"

#include "TurnInPlace.h"
#include "GameFramework/Character.h"
#include "Implementation/TurnInPlaceCharacter.h"
#include "Engine/World.h"

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
	// Return the TurnInPlace component from the owning character, but only if it has valid data
	return TurnCharacterOwner && TurnCharacterOwner->TurnInPlace && TurnCharacterOwner->TurnInPlace->HasValidData() ?
		TurnCharacterOwner->TurnInPlace : nullptr;
}

void UTurnInPlaceMovement::UpdateLastInputVector()
{
	// If we're orienting to movement, we need to update the LastInputVector
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
			const bool bRootMotionNotRecentlyApplied = GetWorld()->TimeSince(LastRootMotionTime) >= 0.25f;  // Grace period for root motion to stop affecting velocity significantly
			const bool bFromAcceleration = !FMath::IsNearlyZero(ComputeAnalogInputModifier(), 0.5f);
			const bool bFromVelocity = !Velocity.IsNearlyZero(GetMaxSpeed() * 0.05f) && bRootMotionNotRecentlyApplied;
			if (bFromAcceleration)
			{
				LastInputVector = Acceleration.GetSafeNormal();
			}
			else if (bFromVelocity)
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
		// Set LastInputVector to the component forward vector if we're not orienting to movement
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
	// If we're not moving, we can use the idle rotation rate
	if (IsMovingOnGround() && Velocity.IsNearlyZero())
	{
		return RotationRateIdle;
	}

	// Use the default rotation rate when moving
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
	// Repeat the checks from Super::PhysicsRotation
	if (!(bOrientRotationToMovement || bUseControllerDesiredRotation))
	{
		return;
	}

	if (!HasValidData() || (!CharacterOwner->Controller && !bRunPhysicsWithNoController))
	{
		return;
	}

	// Allow the turn in place system to handle rotation if desired
	if (UTurnInPlace* TurnInPlace = GetTurnInPlace())
	{
		const float LastTurnOffset = TurnInPlace->TurnOffset;
		
		// We will abort handling if not stationary or not rotating to the last input vector
		if (!TurnInPlace->PhysicsRotation(this, DeltaTime, bRotateToLastInputVector, LastInputVector))
		{
			// Let CMC handle the rotation
			Super::PhysicsRotation(DeltaTime);
		}
	
		// Replicate the turn offset to simulated proxies
		TurnInPlace->PostTurnInPlace(LastTurnOffset);
	}
	else
	{
		Super::PhysicsRotation(DeltaTime);
	}
}

class FNetworkPredictionData_Client* UTurnInPlaceMovement::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UTurnInPlaceMovement* MutableThis = const_cast<UTurnInPlaceMovement*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Character_TurnInPlace(*this);
	}

	return ClientPredictionData;
}

UTurnInPlace* FSavedMove_Character_TurnInPlace::GetTurnInPlace(const ACharacter* C)
{
	UTurnInPlaceMovement* MoveComp = C ? Cast<UTurnInPlaceMovement>(C->GetCharacterMovement()) : nullptr;
	return MoveComp ? MoveComp->GetTurnInPlace() : nullptr;
}

void FSavedMove_Character_TurnInPlace::Clear()
{
	Super::Clear();

	LastAppliedTurnYaw = 0.f;
}

void FSavedMove_Character_TurnInPlace::SetInitialPosition(ACharacter* C)
{
	Super::SetInitialPosition(C);

	if (UTurnInPlace* TurnInPlace = GetTurnInPlace(C))
	{
		LastAppliedTurnYaw = TurnInPlace->LastAppliedTurnYaw;
	}
}

void FSavedMove_Character_TurnInPlace::CombineWith(const FSavedMove_Character* OldMove, ACharacter* C,
	APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, C, PC, OldStartLocation);

	if (UCharacterMovementComponent* MoveComp = C ? C->GetCharacterMovement() : nullptr)
	{
		// Turn in place not supported when we are attached to a moving parent
		if (!StartAttachParent.Get())
		{
			/*
			 * When combining moves, the rotation is set back to StartRotation which will discard our turn in place rotation
			 * So we need to save our turn in place at the same position, and then apply it back over the top after the rotation is set
			 *
			 * The result of not doing this would be that when combining moves (by default FPS >60), approximately half
			 * the turn in place angle would be lost only on the local client, i.e. they would rotate half as much as the server
			 */
			const FSavedMove_Character_TurnInPlace* SavedOldMove = static_cast<const FSavedMove_Character_TurnInPlace*>(OldMove);
			const FRotator NewStartRotation = (SavedOldMove->StartRotation + FRotator(0.f, SavedOldMove->LastAppliedTurnYaw, 0.f)).GetNormalized();
			MoveComp->UpdatedComponent->SetWorldRotation(NewStartRotation, false, nullptr, MoveComp->GetTeleportType());
		}
	}
}

FSavedMovePtr FNetworkPredictionData_Client_Character_TurnInPlace::AllocateNewMove()
{
	return MakeShared<FSavedMove_Character_TurnInPlace>();
}
