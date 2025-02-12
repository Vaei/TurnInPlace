// Copyright (c) 2025 Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "TurnInPlaceTags.h"
#include "TurnInPlaceTypes.generated.h"

class UAnimSequence;
class UAnimMontage;

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

/**
 * Compilation of typical movement setups for easy selection and toggling of properties
 */
UENUM(BlueprintType)
enum class ECharacterMovementType : uint8
{
	OrientToMovement		UMETA(Tooltip = "Orient towards our movement direction. Use bOrientRotationToMovement, disable bUseControllerDesiredRotation and bUseControllerRotationYaw. Updated in UCharacterMovementComponent::PhysicsRotation()"),
	StrafeDesired			UMETA(Tooltip = "Strafing with smooth interpolation to direction based on RotationRate. Use bUseControllerDesiredRotation, disable bUseControllerRotationYaw and bOrientRotationToMovement. Updated in UCharacterMovementComponent::PhysicsRotation()"),
	StrafeDirect 			UMETA(Tooltip = "Strafing with instant snap to direction. Use bUseControllerRotationYaw, disable bUseControllerDesiredRotation and bOrientRotationToMovement. Updated in ACharacter::FaceRotation()"),
};

/**
 * Two functions are responsible for the rotation of a Character:
 * ACharacter::FaceRotation and UCharacterMovementComponent::PhysicsRotation
 * Used to determine which is the correction function to use
 *
 * ECharacterMovementType::OrientToMovement and ECharacterMovementType::StrafeDesired will use UCharacterMovementComponent::PhysicsRotation()
 * ECharacterMovementType::StrafeDirect will use ACharacter::FaceRotation()
 */
UENUM(BlueprintType)
enum class ETurnMethod : uint8
{
	None				UMETA(Tooltip = "No turn in place"),
	FaceRotation		UMETA(Tooltip = "Use ACharacter::FaceRotation"),
	PhysicsRotation		UMETA(Tooltip = "Use UCharacterMovementComponent::PhysicsRotation"),
};

/**
 * Override the Turn In Place parameters to force turn in place to be enabled, locked, or paused.
 */
UENUM(BlueprintType)
enum class ETurnInPlaceOverride : uint8
{
	Default				UMETA(Tooltip = "Process Turn In Place as normal based on the FTurnInPlaceParams from the FTurnInPlaceAnimSet"),
	ForceEnabled		UMETA(Tooltip = "Enabled regardless of FTurnInPlaceParams"),
	ForceLocked			UMETA(ToolTip = "Locked in place and will not rotate regardless of FTurnInPlaceParams"),
	ForcePaused			UMETA(ToolTip = "Will not accumulate any turn offset, allowing normal behaviour expected of a system without any turn in place. Useful for root motion montages")
};

/**
 * State of the Turn In Place system
 * Locking the character in place will prevent any rotation from occurring
 * Pausing the character will prevent any turn offset from accumulating
 */
UENUM(BlueprintType)
enum class ETurnInPlaceEnabledState : uint8
{
	Enabled				UMETA(Tooltip = "Enabled"),
	Locked				UMETA(Tooltip = "Locked in place and will not rotate"),
	Paused				UMETA(Tooltip = "Will not accumulate any turn offset, allowing normal behaviour expected of a system without any turn in place. Useful for root motion montages"),
};

/**
 * How to select the turn animation based on the turn angle
 */
UENUM(BlueprintType)
enum class ETurnAnimSelectMode : uint8
{
	Greater			UMETA(Tooltip = "Get the highest animation that exceeds the turn angle (at 175.f, use 135 turn instead of 180)"),
	Nearest			UMETA(Tooltip = "Get the closest matching animation (at 175.f, use 180 turn). This can result in over-stepping the turn and subsequently turning back again especially when using 45 degree increments; recommend using a min turn angle greater than the smallest animation for better results"),
};

/**
 * Compressed representation of Turn in Place for replication to Simulated Proxies with significant compression
 * to reduce network bandwidth
 */
USTRUCT()
struct ACTORTURNINPLACE_API FTurnInPlaceSimulatedReplication
{
	GENERATED_BODY()

	FTurnInPlaceSimulatedReplication()
		: TurnOffset(0)
	{}

	/** Compressed turn offset */
	UPROPERTY()
	uint16 TurnOffset;

	/** Compress the turn offset from float to short */
	void Compress(float Angle)
	{
		TurnOffset = FRotator::CompressAxisToShort(Angle);
	}

	/** Decompress the turn offset from short to float */
	float Decompress() const
	{
		const float Decompressed = FRotator::DecompressAxisFromShort(TurnOffset);
		return FRotator::NormalizeAxis(Decompressed);
	}
};

/**
 * Settings for Turn In Place
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceSettings
{
	GENERATED_BODY()

	FTurnInPlaceSettings()
		: TurnYawCurveName("RemainingTurnYaw")
		, TurnWeightCurveName("TurnYawWeight")
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
};

/**
 * These properties are used to determine how the turn in place system behaves when under the control of root motion
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceMontageHandling
{
	GENERATED_BODY()

	FTurnInPlaceMontageHandling()
		: bIgnoreAdditiveMontages(true)
		, IgnoreMontageSlots({ TEXT("UpperBody"), TEXT("UpperBodyAdditive"), TEXT("UpperBodyDynAdditiveBase"), TEXT("UpperBodyDynAdditive"), TEXT("Attack") })
	{}

	/** Montages with additive tracks will not be considered to be Playing @see UAnimInstance::IsAnyMontagePlaying() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	bool bIgnoreAdditiveMontages;

	/** Montages using these slots will not be considered to be Playing @see UAnimInstance::IsAnyMontagePlaying() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	TArray<FName> IgnoreMontageSlots;

	/** Montages added here not be considered to be Playing @see UAnimInstance::IsAnyMontagePlaying() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	TArray<UAnimMontage*> IgnoreMontages;
};

/**
 * Minimum and maximum turn angles
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceAngles
{
	GENERATED_BODY()

	FTurnInPlaceAngles(float InMinTurnAngle = 60.f, float InMaxTurnAngle = 0.f)
		: MinTurnAngle(InMinTurnAngle)
		, MaxTurnAngle(InMaxTurnAngle)
	{}
	
	/** Angle at which turn in place will trigger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(UIMin="0", ClampMin="0", UIMax="180", ClampMax="180", Delta="1", ForceUnits="degrees"))
	float MinTurnAngle;

	/**
	 * Maximum angle at which point the character will turn to maintain this value (hard clamp on angle)
	 * Set to 0.0 to disable
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(UIMin="0", ClampMin="0", UIMax="180", ClampMax="180", Delta="1", ForceUnits="degrees"))
	float MaxTurnAngle;
};

/**
 * Turn in place parameters.
 * Used to determine how the turn in place system behaves especially in the
 * context of different animation states.
 */
USTRUCT(BlueprintType)
struct ACTORTURNINPLACE_API FTurnInPlaceParams
{
	GENERATED_BODY()

	FTurnInPlaceParams()
		: State(ETurnInPlaceEnabledState::Enabled)
		, SelectMode(ETurnAnimSelectMode::Greater)
		, SelectOffset(0.f)
		, StepSizes({ 60, 90, 180 })
		, MovingInterpOutRate(1.f)
	{
		TurnAngles.Add(FTurnInPlaceTags::TurnMode_Movement, FTurnInPlaceAngles(60.f, 0.f));
		TurnAngles.Add(FTurnInPlaceTags::TurnMode_Strafe, FTurnInPlaceAngles(60.f, 135.f));
	}

	/** Enable turn in place */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	ETurnInPlaceEnabledState State;

	/** How to determine which turn animation to play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State!=ETurnInPlaceEnabledState::Paused", EditConditionHides))
	ETurnAnimSelectMode SelectMode;

	/**
	 * When selecting the animation to play, add this value to the current offset.
	 * @warning This can offset the animation far enough that it plays an additional animation to correct the offset
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State!=ETurnInPlaceEnabledState::Paused", EditConditionHides, UIMin="-180", ClampMin="-180", UIMax="180", ClampMax="180", Delta="0.1", ForceUnits="degrees"))
	float SelectOffset;

	/** Turn angles for different movement orientations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State!=ETurnInPlaceEnabledState::Paused", EditConditionHides))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State!=ETurnInPlaceEnabledState::Paused", EditConditionHides, UIMin="0", ClampMin="0", UIMax="180", ClampMax="180", Delta="1", ForceUnits="degrees"))
	TArray<int32> StepSizes;

	/**
	 * This is only used when bUseControllerRotationYaw = true
	 *	Not used for bOrientRotationToMovement or bUseControllerDesiredRotation
	 *	
	 * When we start moving we interpolate out of the turn in place at this rate
	 * Interpolation occurs in a range of 0.0 to 1.0 so low values have a big impact on the rate
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(UIMin="0", ClampMin="0", UIMax="3", Delta="0.1"))
	float MovingInterpOutRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(EditCondition="State!=ETurnInPlaceEnabledState::Paused", EditConditionHides))
	FTurnInPlaceMontageHandling MontageHandling;
};

/**
 * Animation set for turn in place
 * Defines the animations to play and the parameters to use
 * As well as the play rate to use when turning in the opposite direction or at max angle
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

	/** Parameters to use when this anim set is active */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceParams Params;

	/**
	 * When playing a turn animation, if an animation in the opposite direction is triggered, scale by this play rate
	 * Useful for quickly completing a turn that is now going the wrong way
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(UIMin="0", ClampMin="0", UIMax="2", ForceUnits="x"))
	float PlayRateOnDirectionChange;

	/**
	 * Play rate to use when being clamped to max angle
	 * Overall feel is improved if the character starts turning faster
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn, meta=(UIMin="0", ClampMin="0", UIMax="2", ForceUnits="x"))
	float PlayRateAtMaxAngle;

	/**
	 * Don't change the play rate when no longer at max angle for the in-progress turn animation
	 * This helps when the player is using a mouse because it often causes jittering play rate
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	bool bMaintainMaxAnglePlayRate;

	/** Animations to select from when turning left */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	TArray<UAnimSequence*> LeftTurns;

	/** Animations to select from when turning right */
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
	{}

	/**
	 * Remaining turn yaw to complete the turn
	 * This gets deducted from the Turn Offset as the animation continues
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float RemainingTurnYaw;

	/**
	 * Queried to determine if we're in the actual turn vs. recovery frames
	 * Used for transitioning from turn to recovery
	 * And for querying if we're currently turning
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float TurnYawWeight;
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
		, TurnModeTag(FGameplayTag::EmptyTag)
		, bHasValidTurnAngles(false)
	{}

	/** The current Anim Set containing the turn anims to play and turn params */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceAnimSet AnimSet;

	/** Current offset for the turn in place -- this is the inverse of Epic's RootYawOffset (*= -1.0 for same result) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	float TurnOffset;

	/**
	 * True if an animation is currently being played that results in turning in place
	 * This is based on the value of the TurnYawWeight curve
	 */
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

	/** GameplayTag to determine which turn angles to use */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FGameplayTag TurnModeTag;

	/** Cached result for the validity of the contained TurnAngles property */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bHasValidTurnAngles;

	/** Cached TurnAngles */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	FTurnInPlaceAngles TurnAngles;

	/** Cached Settings from the TurnInPlace component */
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

	/** True if we should transition to a turn in place anim state */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bWantsToTurn;

	/** True if we should transition to a turn in place recovery anim state */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bWantsTurnRecovery;

	/** True if we should abort the start state and transition into cycle due to turn angle */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bTransitionStartToCycleFromTurn;

	/** True if we should abort the stop state and transition into idle because needs to turn in place */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
	bool bTransitionStopToIdleForTurn;
};
