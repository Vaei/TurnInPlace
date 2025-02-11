// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TurnInPlaceTypes.h"
#include "GameFramework/Character.h"
#include "TurnInPlaceCharacter.generated.h"

class UTurnInPlaceMovement;
class UTurnInPlace;
/**
 * This character is optional. We don't cast to it in TurnInPlace.
 * You can integrate TurnInPlace into your own character class by copying the functionality.
 * 
 * @note You cannot integrate TurnInPlace in blueprints and must derive this character because you cannot override FaceRotation, etc.
 */
UCLASS(Blueprintable)
class ACTORTURNINPLACE_API ATurnInPlaceCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/**
	 * Turn in place component
	 * We do not create it here, but use FindComponentByClass() in PreInitializeComponents() that it can be added to
	 * the character in Blueprint instead to allow for Blueprint derived components
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Turn)
	TObjectPtr<UTurnInPlace> TurnInPlace;

	/** Movement component used for movement logic in various movement modes (walking, falling, etc), containing relevant settings and functions to control movement. */
	UPROPERTY(BlueprintReadOnly, Category=Character)
	TObjectPtr<UTurnInPlaceMovement> TurnInPlaceMovement;

	/** Name of the TurnInPlace component. Use this name if you want to prevent creation of the component (with ObjectInitializer.DoNotCreateDefaultSubobject). */
	static FName TurnInPlaceComponentName;
	
public:
	ATurnInPlaceCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

public:
	/**
	 * Character is currently turning in place if the TurnYawWeight curve is not 0
	 * @return True if the character is currently turning in place
	 */
	UFUNCTION(BlueprintPure, Category=Turn)
	bool IsTurningInPlace() const;

	/**
	 * Called by ACharacter::FaceRotation() to handle turn in place rotation
	 * @return True if FaceRotation is handled
	 */
	virtual bool TurnInPlaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f);

	/**
	 * Overrides ACharacter::FaceRotation() to handle turn in place rotation
	 */
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;

	virtual void Tick(float DeltaTime) override;
};
