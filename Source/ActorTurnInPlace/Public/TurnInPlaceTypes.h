// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "TurnInPlaceTags.h"
#include "TurnInPlaceTypes.generated.h"

/**
 * SetActorRotation always performs a sweep even for yaw-only rotations which cannot reasonably collide
 * Use the functions using SweepHandling to improve the behaviour of SetActorRotation
 */
UENUM(BlueprintType)
enum class ERotationSweepHandling : uint8
{
	AutoDetect			UMETA(Tooltip = "Only perform a sweep if the rotation delta contains pitch or roll"),
	AlwaysSweep			UMETA(Tooltip = "Always perform a sweep when rotating"),
	NeverSweep			UMETA(Tooltip = "Never perform a sweep when rotating"),
};

UENUM(BlueprintType)
enum class ETurnMethod : uint8
{
	None				UMETA(Tooltip = "No turn in place"),
	FaceRotation		UMETA(Tooltip = "Use ACharacter::FaceRotation"),
	PhysicsRotation		UMETA(Tooltip = "Use UCharacterMovementComponent::PhysicsRotation"),
};

UENUM(BlueprintType)
enum class ETurnInPlaceOverride : uint8
{
	Default				UMETA(Tooltip = "Use FTurnInPlaceParams"),
	ForceEnabled		UMETA(Tooltip = "Enabled regardless of FTurnInPlaceParams"),
	ForceLocked			UMETA(ToolTip = "Locked in place and will not rotate regardless of FTurnInPlaceParams"),
	ForcePaused			UMETA(ToolTip = "Will not accumulate any turn offset, allowing normal behaviour expected of a system without any turn in place. Useful for root motion montages")
};

UENUM(BlueprintType)
enum class ETurnInPlaceEnabledState : uint8
{
	Enabled				UMETA(Tooltip = "Enabled"),
	Locked				UMETA(Tooltip = "Locked in place and will not rotate"),
	Paused				UMETA(Tooltip = "Will not accumulate any turn offset, allowing normal behaviour expected of a system without any turn in place. Useful for root motion montages"),
};

UENUM(BlueprintType)
enum class ETurnAnimSelectMode : uint8
{
	Greater			UMETA(Tooltip = "Get the highest animation that exceeds the turn angle (at 175.f, use 135 turn instead of 180)"),
	Nearest			UMETA(Tooltip = "Get the closest matching animation (at 175.f, use 180 turn). This can result in over-stepping the turn and subsequently turning back again especially when using 45 degree increments; recommend using a min turn angle greater than the smallest animation for better results"),
};

UENUM(BlueprintType)
enum class EInterpOutMode : uint8
{
	Interpolation		UMETA(Tooltip = "Interpolate away the turn rotation while moving"),
	AnimationCurve		UMETA(Tooltip = "Use start or pivot animation rotation curve to remove rotation"),
};

/**
 * Compressed representation of Turn in Place for replication to Simulated Proxies with significant compression
 * to reduce network bandwidth
 * Set simulated rotation time to 0.2 roughly for dedicated server (default 0.05 will jitter)
 *
 * Add this to your ACharacter class as ReplicatedTurnOffset
 * Have it ReplicatedUsing=OnRep_ReplicatedTurnOffset
 *
 * Add it to GetLifetimeReplicatedProps: DOREPLIFETIME_CONDITION(ThisClass, ReplicatedTurnOffset, COND_SimulatedOnly);
 *
 * In ACharacter::FaceRotation, at the end, use the following snippet:
 *
 * 	if (HasAuthority() && GetNetMode() != NM_Standalone)
 *	{
 *		ReplicatedTurnOffset.Compress(TurnInPlace.TurnOffset);
 *	}
 *
 *	In OnRep_ReplicatedTurnOffset() use this:
 *	TurnInPlace.TurnOffset = ReplicatedTurnOffset.Decompress();
 */
USTRUCT()
struct ACTORTURNINPLACE_API FTurnInPlaceSimulatedReplication
{
	GENERATED_BODY()

	FTurnInPlaceSimulatedReplication()
		: TurnOffset(0)
	{}

	UPROPERTY()
	uint16 TurnOffset;

	void Compress(float Angle)
	{
		TurnOffset = FRotator::CompressAxisToShort(Angle);
	}

	float Decompress() const
	{
		const float Decompressed = FRotator::DecompressAxisFromShort(TurnOffset);
		return FRotator::NormalizeAxis(Decompressed);
	}
};

/**
 * Separated so they can be passed through easily when changing animation layers
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceSettings
{
	GENERATED_BODY()

	FTurnInPlaceSettings()
		: TurnYawCurveName("RemainingTurnYaw")
		, TurnWeightCurveName("TurnYawWeight")
		, StartYawCurveName("BlendRotation")
	{}

	/**
	 * Name of the curve that represents how much yaw rotation remains to complete the turn
	 * This curve is queried to reduce the turn offset by the same amount of rotation in the animation
	 *
	 * This curve name must be added to Inertialization node FilteredCurves in the animation graph
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName TurnYawCurveName;

	/**
	 * Name of the curve that represents how much of the turn animation's yaw should be applied to the TurnOffset
	 * This curve is used to reduce the amount of turning and blend into recovery (when the yaw is no longer applied
	 * it continues playing the animation but considers itself to be in a state of recovery where it plays out
	 * the remaining frames, but can also early exit if the player continues to turn)
	 *
	 * This curve name must be added to Inertialization node FilteredCurves in the animation graph
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName TurnWeightCurveName;

	/**
	 * Name of the curve that represents how much of the turn animation's yaw should be removed when we start moving
	 *
	 * This curve name must be added to Inertialization node FilteredCurves in the animation graph
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings)
	FName StartYawCurveName;
};

USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceAngles
{
	GENERATED_BODY()

	FTurnInPlaceAngles(float InMinTurnAngle = 60.f, float InMaxTurnAngle = 0.f)
		: MinTurnAngle(InMinTurnAngle)
		, MaxTurnAngle(InMaxTurnAngle)
	{}
	
	/** Angle at which turn in place will trigger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta = (UIMin = "0", ClampMin = "0"))
	float MinTurnAngle;

	/**
	 * Maximum angle at which point the character will turn to maintain this value (hard clamp on angle)
	 * Set to 0.0 to disable
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta = (UIMin = "0", ClampMin = "0"))
	float MaxTurnAngle;
};

USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceParams
{
	GENERATED_BODY()

	FTurnInPlaceParams()
		: State(ETurnInPlaceEnabledState::Enabled)
		, SelectMode(ETurnAnimSelectMode::Greater)
		, SelectOffset(0.f)
		, StepSizes({ 60, 90, 180 })
		, MovementInterpOutMode(EInterpOutMode::Interpolation)
		, StrafeInterpOutMode(EInterpOutMode::Interpolation)
		, MovementInterpOutRate(1.f)
		, StrafeInterpOutRate(1.f)
		, bIgnoreAdditiveMontages(true)
		, IgnoreMontageSlots({ TEXT("UpperBody"), TEXT("UpperBodyAdditive"), TEXT("UpperBodyDynAdditiveBase"), TEXT("UpperBodyDynAdditive"), TEXT("Attack") })
	{
		TurnAngles.Add(FTurnInPlaceTags::TurnMode_Movement, FTurnInPlaceAngles(60.f, 0.f));
		TurnAngles.Add(FTurnInPlaceTags::TurnMode_Strafe, FTurnInPlaceAngles(60.f, 135.f));
	}

	/** Enable turn in place */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	ETurnInPlaceEnabledState State;

	/** How to determine which turn animation to play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	ETurnAnimSelectMode SelectMode;

	/**
	 * When selecting the animation to play, add this value to the current offset.
	 * @warning This can offset the animation far enough that it plays an additional animation to correct the offset
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	float SelectOffset;

	/** Turn angles for different movement orientations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	TMap<FGameplayTag, FTurnInPlaceAngles> TurnAngles;

	const FTurnInPlaceAngles* GetTurnAngles(const FGameplayTag& TurnModeTag) const
	{
		// Return this turn angle if available
		if (const FTurnInPlaceAngles* Angles = TurnAngles.Find(TurnModeTag))
		{
			return Angles;
		}
		return nullptr;
	}

	/**
	 * Yaw angles where different step animations occur
	 * Corresponding animations must be present for the anim graph to play
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	TArray<int32> StepSizes;

	/** How to remove turn offset when starting to move */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	EInterpOutMode MovementInterpOutMode;

	/** How to remove turn offset when starting to move while strafing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	EInterpOutMode StrafeInterpOutMode;

	/**
	 * When we start moving we interpolate out of the turn in place at this rate
	 * Interpolation occurs in a range of 0.0 to 1.0 so low values have a big impact on the rate
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled&&MovementInterpOutMode==EInterpOutMode::Interpolation", EditConditionHides, UIMin="0", ClampMin="0"))
	float MovementInterpOutRate;

	/**
	 * When we start moving we interpolate out of the turn in place at this rate
	 * Interpolation occurs in a range of 0.0 to 1.0 so low values have a big impact on the rate
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled&&StrafeInterpOutMode==EInterpOutMode::Interpolation", EditConditionHides, UIMin="0", ClampMin="0"))
	float StrafeInterpOutRate;

	/** Montages with additive tracks will not be considered to be Playing @see UAnimInstance::IsAnyMontagePlaying() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	bool bIgnoreAdditiveMontages;

	/** Montages using these slots will not be considered to be Playing @see UAnimInstance::IsAnyMontagePlaying() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	TArray<FName> IgnoreMontageSlots;

	/** Montages added here not be considered to be Playing @see UAnimInstance::IsAnyMontagePlaying() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State==ETurnInPlaceEnabledState::Enabled", EditConditionHides))
	TArray<UAnimMontage*> IgnoreMontages;
};

/**
 * Animation setup data
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceAnimSet
{
	GENERATED_BODY()

	FTurnInPlaceAnimSet()
		: Params({})
		, PlayRateOnDirectionChange(1.7f)
		, PlayRateAtMaxAngle(1.3f)
		, bMaintainMaxAnglePlayRate(true)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceParams Params;

	/**
	 * When playing a turn animation, if an animation in the opposite direction is triggered, scale by this play rate
	 * Useful for quickly completing a turn that is now going the wrong way
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	float PlayRateOnDirectionChange;

	/**
	 * Play rate to use when being clamped to max angle
	 * Overall feel is improved if the character starts turning faster
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	float PlayRateAtMaxAngle;

	/**
	 * Don't change the play rate when no longer at max angle for the in-progress turn animation
	 * This helps when the player is using a mouse because it often causes jittering play rate
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	bool bMaintainMaxAnglePlayRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	TArray<UAnimSequence*> LeftTurns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	TArray<UAnimSequence*> RightTurns;
};

/**
 * Cached in NativeThreadSafeUpdateAnimation or BlueprintThreadSafeUpdateAnimation
 * Avoid updating these out of sync with the anim graph by caching them in a consistent position thread-wise
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceCurveValues
{
	GENERATED_BODY()
	
	FTurnInPlaceCurveValues()
		: RemainingTurnYaw(0.f)
		, TurnYawWeight(0.f)
		, BlendRotation(0.f)
	{}

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float RemainingTurnYaw;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float TurnYawWeight;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float BlendRotation;
};

/**
 * Retrieves game thread data in NativeUpdateAnimation or BlueprintUpdate Animation
 * For processing by FTurnInPlaceAnimGraphOutput in NativeThreadSafeUpdateAnimation or BlueprintThreadSafeUpdateAnimation
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceAnimGraphData
{
	GENERATED_BODY()

	FTurnInPlaceAnimGraphData()
		: TurnOffset(0)
		, bIsTurning(false)
		, bWantsToTurn(false)
		, bTurnRight(false)
		, StepSize(0)
		, bIsStrafing(false)
		, TurnModeTag(FGameplayTag::EmptyTag)
		, bHasValidTurnAngles(false)
	{}

	/** The current Anim Set containing the turn anims to play and turn params */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceAnimSet AnimSet;

	/** Current offset for the turn in place -- this is the inverse of Epic's RootYawOffset (*= -1.0 for same result) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float TurnOffset;

	/** True if an animation is currently being played that results in turning in place */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bIsTurning;

	/** TurnOffset is greater than MinTurnAngle or doing a small turn, used by anim graph to transition to turn */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bWantsToTurn;

	/** True if turning to the right */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bTurnRight;

	/** Which animation to use */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	int32 StepSize;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bIsStrafing;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FGameplayTag TurnModeTag;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bHasValidTurnAngles;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceAngles TurnAngles;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceSettings Settings;
};

/**
 * Processes data from FTurnInPlaceAnimGraphData and returns the output for use in the anim graph
 * This drives anim state transitions and node behaviour
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceAnimGraphOutput
{
	GENERATED_BODY()

	FTurnInPlaceAnimGraphOutput()
		: TurnOffset(0.f)
		, bWantsToTurn(false)
		, bWantsTurnRecovery(false)
		, bTransitionStartToCycleFromTurn(false)
		, bTransitionStopToIdleForTurn(false)
	{}

	/** Current offset for the turn in place */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float TurnOffset;

	/** True if should transition to a turn in place anim state */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bWantsToTurn;

	/** True if should transition to a turn in place recovery anim state */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bWantsTurnRecovery;

	/** True if should abort the start state and transition into cycle due to turn angle */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bTransitionStartToCycleFromTurn;

	/** True if should abort the stop state and transition into idle because needs to turn in place */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bTransitionStopToIdleForTurn;
};
