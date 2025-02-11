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

void FSavedMove_Character_TurnInPlace::Clear()
{
	Super::Clear();

	StartTurnOffset = 0.f;
	EndTurnOffset = 0.f;
}

void FSavedMove_Character_TurnInPlace::SetInitialPosition(ACharacter* C)
{
	Super::SetInitialPosition(C);

	UTurnInPlaceMovement* MoveComp = C ? Cast<UTurnInPlaceMovement>(C->GetCharacterMovement()) : nullptr;
	if (UTurnInPlace* TurnInPlace = MoveComp ? MoveComp->GetTurnInPlace() : nullptr)
	{
		StartTurnOffset = TurnInPlace->TurnOffset;
	}
}

void FSavedMove_Character_TurnInPlace::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
	// When considering whether to delay or combine moves, we need to compare the move at the start and the end
	UTurnInPlaceMovement* MoveComp = C ? Cast<UTurnInPlaceMovement>(C->GetCharacterMovement()) : nullptr;
	if (UTurnInPlace* TurnInPlace = MoveComp ? MoveComp->GetTurnInPlace() : nullptr)
	{
		EndTurnOffset = TurnInPlace->TurnOffset;
		
		if (PostUpdateMode == PostUpdate_Record)
		{
			// Don't combine moves if the properties changed over the course of the move
			if (UTurnInPlace::HasTurnOffsetChanged(StartTurnOffset, EndTurnOffset))
			{
				// Turn in place will occur at HALF the angle on the LOCAL CLIENT due to how rotation is handled when combining moves
				bForceNoCombine = true;
			}
		}
	}

	Super::PostUpdate(C, PostUpdateMode);
}

bool FSavedMove_Character_TurnInPlace::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter,
	float MaxDelta) const
{
	const TSharedPtr<FSavedMove_Character_TurnInPlace>& SavedMove = StaticCastSharedPtr<FSavedMove_Character_TurnInPlace>(NewMove);
	
	// Don't combine moves if the properties changed over the course of the move
	if (UTurnInPlace::HasTurnOffsetChanged(StartTurnOffset, SavedMove->StartTurnOffset))
	{
		// Turn in place will occur at HALF the angle on the LOCAL CLIENT due to how rotation is handled when combining moves
		return false;
	}
	
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Character_TurnInPlace::CombineWith(const FSavedMove_Character* OldMove, ACharacter* C,
	APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, C, PC, OldStartLocation);

	UTurnInPlaceMovement* MoveComp = C ? Cast<UTurnInPlaceMovement>(C->GetCharacterMovement()) : nullptr;
	if (UTurnInPlace* TurnInPlace = MoveComp ? MoveComp->GetTurnInPlace() : nullptr)
	{
		const FSavedMove_Character_TurnInPlace* SavedOldMove = static_cast<const FSavedMove_Character_TurnInPlace*>(OldMove);
		TurnInPlace->TurnOffset = SavedOldMove->StartTurnOffset;
	}
}

FSavedMovePtr FNetworkPredictionData_Client_Character_TurnInPlace::AllocateNewMove()
{
	return MakeShared<FSavedMove_Character_TurnInPlace>();
}
