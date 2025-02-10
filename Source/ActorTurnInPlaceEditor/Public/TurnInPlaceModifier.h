// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Editor/AnimationModifiers/Public/AnimationModifier.h"
#include "TurnInPlaceModifier.generated.h"

/**
 * Extract curves from root motion required for turn in place
 */
UCLASS()
class ACTORTURNINPLACEEDITOR_API UTurnInPlaceModifier : public UAnimationModifier
{
	GENERATED_BODY()

public:
	/** Bone we are going to generate the curve from */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName BoneName = TEXT("root");

	/** This curve drives the rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName TurnYawCurveName = TEXT("RemainingTurnYaw");

	/** This curve tells us if the actual turn is in progress, when it changes from 1.0 we can enter recovery anim state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName TurnWeightCurveName = TEXT("TurnYawWeight");

	/**
	 * Prevents the final weight key from being placed too close to the end of the animation
	 * This allows for blend time, it needs to become 0 in time for it to blend out
	 */
	UPROPERTY(EditAnywhere, Category=Settings, meta = (ClampMin = "0", UIMin="0", SliderExponent="0.1"))
	float GraphTransitionBlendTime = 0.2f;

	/**
	 * Prevents the final weight key from being placed too close to the end of the animation
	 * Value of 0.1 can't place the weight any further than 90% of the distance along the animation
	 * 0.0 to effectively disable, this places key at the very end, which is mandatory, or it'll never exit the turn
	 */
	UPROPERTY(EditAnywhere, Category=Settings, meta = (ClampMin = "0", UIMin="0", SliderExponent="0.1"))
	float MaxWeightOffsetPct = 0.0f;

	/** Rate used to sample the animation */
	UPROPERTY(EditAnywhere, Category=Settings, meta = (ClampMin = "1"))
	int32 SampleRate = 60;

protected:
	/**
	* Helper function to extract the pose for a given bone at a given time
	* TAdd a MemMark (FMemMark Mark(FMemStack::Get());) at the correct scope if you are using it from outside world's tick
	*/
	static FTransform ExtractBoneTransform(const UAnimSequence* Animation, const FBoneContainer& BoneContainer, FCompactPoseBoneIndex CompactPoseBoneIndex, float Time, bool bComponentSpace);
	
public:
	virtual void OnApply_Implementation(UAnimSequence* AnimationSequence) override;
	virtual void OnRevert_Implementation(UAnimSequence* AnimationSequence) override;
};
