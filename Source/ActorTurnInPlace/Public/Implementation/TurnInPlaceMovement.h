// Copyright (c) 2025 Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TurnInPlaceMovement.generated.h"

class UTurnInPlace;
class ATurnInPlaceCharacter;
/**
 * This movement component is optional. We don't cast to it in TurnInPlace.
 * 
 * Provides the ability to rotate to the last input vector with a separate idle rotation rate,
 * which is useful for turn in place when using bOrientRotationToMovement
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

public:
	/** Get prediction data for a client game. Should not be used if not running as a client. Allocates the data on demand and can be overridden to allocate a custom override if desired. Result must be a FNetworkPredictionData_Client_Character. */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};

class ACTORTURNINPLACE_API FSavedMove_Character_TurnInPlace : public FSavedMove_Character
{
	using Super = FSavedMove_Character;

public:
	FSavedMove_Character_TurnInPlace()
		: LastAppliedTurnYaw(0)
	{}

	virtual ~FSavedMove_Character_TurnInPlace() override
	{}

	float LastAppliedTurnYaw;

	static UTurnInPlace* GetTurnInPlace(const ACharacter* C);

	/** Clear saved move properties, so it can be re-used. */
	virtual void Clear() override;

	/** Set the properties describing the position, etc. of the moved pawn at the start of the move. */
	virtual void SetInitialPosition(ACharacter* C) override;

	/** Combine this move with an older move and update relevant state. */
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;
};

class ACTORTURNINPLACE_API FNetworkPredictionData_Client_Character_TurnInPlace : public FNetworkPredictionData_Client_Character
{
	using Super = FNetworkPredictionData_Client_Character;

public:
	FNetworkPredictionData_Client_Character_TurnInPlace(const UCharacterMovementComponent& ClientMovement)
		: Super(ClientMovement)
	{}

	virtual FSavedMovePtr AllocateNewMove() override;
};