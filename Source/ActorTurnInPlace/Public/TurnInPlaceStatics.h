// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "TurnInPlaceTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TurnInPlaceStatics.generated.h"

class UTurnInPlace;

/**
 * Blueprint function library for TurnInPlace.
 */
UCLASS()
class ACTORTURNINPLACE_API UTurnInPlaceStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * SetActorRotation always performs a sweep even for yaw-only rotations which cannot reasonably collide
	 * Use this function using SweepHandling to improve the behaviour of SetActorRotation
	 */
	UFUNCTION(BlueprintCallable, Category=Turn, meta=(DefaultToSelf="Character"))
	static bool SetCharacterRotation(ACharacter* Character, const FRotator& NewRotation, ETeleportType Teleport = ETeleportType::None, ERotationSweepHandling SweepHandling = ERotationSweepHandling::AutoDetect);

	UFUNCTION(BlueprintCallable, Category=Turn, meta=(DefaultToSelf="Character"))
	static void SetCharacterMovementType(ACharacter* Character, ECharacterMovementType MovementType);
	
public:
	/**
	 * Calculate the turn in place play rate.
	 * Increases the play rate when max angle is reached or we've changed directions while currently already in a turn (that is the wrong direction)
	 * ForceMaxAngle allows us to force the play rate to be at the max angle until we complete our current turn, this can prevent rapidly toggling play rates which occurs with a mouse
	 * 
	 * @param AnimGraphData The anim graph data.
	 * @param bForceTurnRateMaxAngle True to force the turn rate max angle.
	 * @param bHasReachedMaxAngle True if the max angle has been reached.
	 * @return The turn in place play rate.
	 */
	UFUNCTION(BlueprintCallable, Category=Turn, meta=(BlueprintThreadSafe, DisplayName="Get Turn In Place Play Rate (Thread Safe)"))
	static float GetTurnInPlacePlayRate_ThreadSafe(const FTurnInPlaceAnimGraphData& AnimGraphData,
		bool bForceTurnRateMaxAngle, bool& bHasReachedMaxAngle);

	UFUNCTION(BlueprintPure, Category=Turn, meta=(BlueprintThreadSafe, DisplayName="Get Updated Turn In Place Anim Time (Thread Safe)"))
	static float GetUpdatedTurnInPlaceAnimTime_ThreadSafe(const UAnimSequence* TurnAnimation, float CurrentAnimTime, float DeltaTime, float TurnPlayRate);
	
	/** 
	 * Get the animation sequence play rate.
	 * 
	 * @param Animation The animation sequence.
	 * @return The animation sequence play rate.
	 */
	UFUNCTION(BlueprintPure, Category=Animation, meta=(BlueprintThreadSafe, DisplayName="Get Animation Sequence Play Rate (Thread Safe)"))
	static float GetAnimationSequencePlayRate(const UAnimSequenceBase* Animation);

	/** Useful function for debugging the animation assigned to sequence evaluators and players using LogString */
	UFUNCTION(BlueprintPure, Category=Animation, meta=(BlueprintThreadSafe, DisplayName="Get Animation Sequence Name (Thread Safe)"))
	static FString GetAnimationSequenceName(const UAnimSequenceBase* Animation);
	
	/** Execute all turn in place debug commands */
	UFUNCTION(BlueprintCallable, Category=Turn, meta=(WorldContext="WorldContextObject"))
	static void DebugTurnInPlace(UObject* WorldContextObject, bool bDebug);

public:
	/**
	 * Update anim graph data for turn in place by retrieving data from the game thread. Call from NativeUpdateAnimation or BlueprintUpdateAnimation.
	 * @param TurnInPlace The turn in place component
	 * @param AnimGraphData The anim graph data for this frame
	 * @param bCanUpdateTurnInPlace True if the turn in place is valid, false if we should not process turn in place this frame 
	 */
	UFUNCTION(BlueprintCallable, Category=Turn, meta=(NotBlueprintThreadSafe, DisplayName="Update Turn In Place"))
	static void UpdateTurnInPlace(UTurnInPlace* TurnInPlace, FTurnInPlaceAnimGraphData& AnimGraphData,
		bool& bCanUpdateTurnInPlace);

	/**
	 * Process anim graph data that was retrieved from the game thread. Call from NativeThreadSafeUpdateAnimation or BlueprintThreadSafeUpdateAnimation.
	 * @param AnimGraphData The anim graph data for this frame from UpdateTurnInPlace
	 * @param bCanUpdateTurnInPlace True if the turn in place is valid, false if we should not process turn in place this frame
	 * @param bIsStrafing True if the character is strafing - bTransitionStartToCycleFromTurn should only be used when strafing
	 * @return The processed turn in place data with necessary output values for the anim graph
	 */
	UFUNCTION(BlueprintCallable, Category=Turn, meta=(BlueprintThreadSafe, DisplayName="Thread Safe Update Turn In Place"))
	static FTurnInPlaceAnimGraphOutput ThreadSafeUpdateTurnInPlace(const FTurnInPlaceAnimGraphData& AnimGraphData,
		bool bCanUpdateTurnInPlace, bool bIsStrafing);

	UFUNCTION(BlueprintCallable, Category=Turn, meta=(BlueprintThreadSafe, DefaultToSelf="AnimInstance", DisplayName="Thread Safe Update Turn In Place Curve Values"))
	static FTurnInPlaceCurveValues ThreadSafeUpdateTurnInPlaceCurveValues(const UAnimInstance* AnimInstance, const FTurnInPlaceAnimGraphData& AnimGraphData);
};
