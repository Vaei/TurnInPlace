// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TurnInPlaceMovement.generated.h"


class UTurnInPlace;
class ATurnInPlaceCharacter;
/**
 * This movement component is optional.
 * It will provide the ability to rotate to the last input vector with a separate idle rotation rate,
 * which is useful for turn in place that is non-strafing
 */
UCLASS()
class ACTORTURNINPLACE_API UTurnInPlaceMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/**
	 * If true, when input is released will continue rotating in that direction
	 * Only applied if bOrientRotationToMovement is true
	 */
	UPROPERTY(Category = "Character Movement (Rotation Settings)", EditAnywhere, BlueprintReadWrite)
	bool bRotateToLastInputVector = true;

	/** Change in rotation per second, used when UseControllerDesiredRotation or OrientRotationToMovement are true. Set a negative value for infinite rotation rate and instant turns. */
	UPROPERTY(Category="Character Movement (Rotation Settings)", EditAnywhere, BlueprintReadWrite)
	FRotator RotationRateIdle = { 0.f, 1150.f, 0.f };

public:
	/**
	 * Cached in ApplyRootMotionToVelocity(). Typically, it would be CalcVelocity() but it is not called while we're
	 * under the effects of root motion
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character Movement (Rotation Settings)")
	FVector LastInputVector = FVector::ZeroVector;

	/**
	 * Last time root motion was applied
	 * Used for LastInputVector handling
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Character Movement (Rotation Settings)")
	float LastRootMotionTime = 0.f;

public:
	/** Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<ATurnInPlaceCharacter> TurnCharacterOwner;

public:
	virtual void PostLoad() override;
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	/** Get the TurnInPlace component from the owning character. Returns nullptr if the Component contains invalid data */
	UTurnInPlace* GetTurnInPlace() const;

public:
	/** Maintain the LastInputVector so we can rotate towards it */
	void UpdateLastInputVector();

	/** Update the LastInputVector here because CalcVelocity() is not called while under the effects of root motion */
	virtual void ApplyRootMotionToVelocity(float DeltaTime) override;

	/** Virtual getter for rotation rate to vary rotation rate based on the current state */
	virtual FRotator GetRotationRate() const;
	virtual FRotator GetDeltaRotation(float DeltaTime) const override;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;

	/** Handle rotation based on the TurnInPlace component */
	virtual void PhysicsRotation(float DeltaTime) override;
};
