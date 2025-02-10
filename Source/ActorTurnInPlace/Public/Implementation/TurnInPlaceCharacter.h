// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TurnInPlaceTypes.h"
#include "GameFramework/Character.h"
#include "TurnInPlaceCharacter.generated.h"

class UTurnInPlaceMovement;
class UTurnInPlace;
/**
 * This character is optional.
 * You can integrate TurnInPlace into your own character class by copying the functionality.
 * 
 * @note You cannot integrate TurnInPlace in blueprints and must derive this character because you cannot override FaceRotation, etc.
 */
UCLASS(Blueprintable)
class ACTORTURNINPLACE_API ATurnInPlaceCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UTurnInPlace> TurnInPlace;
	
	UPROPERTY(BlueprintReadOnly, Category=Character)
	TObjectPtr<UTurnInPlaceMovement> TurnInPlaceMovement;

protected:
	UPROPERTY(ReplicatedUsing=OnRep_SimulatedTurnOffset)
	FTurnInPlaceSimulatedReplication SimulatedTurnOffset;
	
public:
	ATurnInPlaceCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void PreInitializeComponents() override;

	UFUNCTION()
	void OnRep_SimulatedTurnOffset();

public:
	/**
	 * Character is currently turning in place if the TurnYawWeight curve is not 0
	 * @return True if the character is currently turning in place
	 */
	UFUNCTION(BlueprintPure, Category=Turn)
	bool IsTurningInPlace() const;

	/**
	 * SetActorRotation always performs a sweep even for yaw-only rotations which cannot reasonably collide
	 * Use this function using SweepHandling to improve the behaviour of SetActorRotation
	 */
	bool SetCharacterRotation(const FRotator& NewRotation, ETeleportType Teleport = ETeleportType::None,
		ERotationSweepHandling SweepHandling = ERotationSweepHandling::AutoDetect);

	/** @return True if FaceRotation is handled */
	virtual bool TurnInPlaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f);
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;
	void SuperFaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f);
};
