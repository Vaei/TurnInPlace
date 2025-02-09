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
	UPROPERTY(BlueprintReadOnly, Category = "Character Movement (Rotation Settings)")
	FVector LastInputVector = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Character Movement (Rotation Settings)")
	float LastRootMotionTime = 0.f;

public:
	/** Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<ATurnInPlaceCharacter> TurnCharacterOwner;

public:
	virtual void PostLoad() override;
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	UTurnInPlace* GetTurnInPlace() const;

public:
	void UpdateLastInputVector();

	virtual void ApplyRootMotionToVelocity(float DeltaTime) override;

	virtual FRotator GetRotationRate() const;
	virtual FRotator GetDeltaRotation(float DeltaTime) const override;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;

	/** Override if a pivot state is available */
	virtual bool IsPivoting() const { return false; }

	virtual bool ShouldReinitializeTurnRotation() const { return false; }
	
	virtual void PhysicsRotation(float DeltaTime) override;
};
