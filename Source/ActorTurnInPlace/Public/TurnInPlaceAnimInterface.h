// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TurnInPlaceTypes.h"
#include "UObject/Interface.h"
#include "TurnInPlaceAnimInterface.generated.h"

UINTERFACE()
class UTurnInPlaceAnimInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implement this interface into your character's animation blueprint.
 */
class ACTORTURNINPLACE_API ITurnInPlaceAnimInterface
{
	GENERATED_BODY()

public:
	/**
	 * Get the current turn in place anim set.
	 * You must maintain thread safety when implementing this function.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category=Turn, meta=(BlueprintThreadSafe))
	FTurnInPlaceAnimSet GetTurnInPlaceAnimSet() const;
	virtual FTurnInPlaceAnimSet GetTurnInPlaceAnimSet_Implementation() const { return {}; }

	/**
	 * Get the cached turn in place curve values.
	 * These should have been cached in NativeThreadSafeUpdateAnimation or BlueprintThreadSafeUpdateAnimation
	 * Avoid updating these out of sync with the anim graph by caching them in a consistent position thread-wise
	 * You must maintain thread safety when implementing this function.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category=Turn, meta=(BlueprintThreadSafe))
	FTurnInPlaceCurveValues GetTurnInPlaceCurveValues() const;
	virtual FTurnInPlaceCurveValues GetTurnInPlaceCurveValues_Implementation() const { return {}; }
};
