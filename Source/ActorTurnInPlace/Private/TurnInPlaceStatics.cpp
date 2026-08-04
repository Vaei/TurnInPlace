// Copyright (c) 2025 Jared Taylor


#include "TurnInPlaceStatics.h"

#include "TurnInPlace.h"
#include "TurnInPlaceTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "EngineDefines.h"  // For UE_ENABLE_DEBUG_DRAWING

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlaceStatics)

void UTurnInPlaceStatics::SetCharacterMovementType(ACharacter* Character, ECharacterMovementType MovementType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::SetCharacterMovementType);
	
	if (IsValid(Character) && Character->GetCharacterMovement())
	{
		switch (MovementType)
		{
		case ECharacterMovementType::OrientToMovement:
			Character->bUseControllerRotationYaw = false;
		    Character->GetCharacterMovement()->bOrientRotationToMovement = true;
			Character->GetCharacterMovement()->bUseControllerDesiredRotation = false;
			break;
		case ECharacterMovementType::StrafeDesired:
			Character->bUseControllerRotationYaw = false;
			Character->GetCharacterMovement()->bOrientRotationToMovement = false;
			Character->GetCharacterMovement()->bUseControllerDesiredRotation = true;
			break;
		case ECharacterMovementType::StrafeDirect:
			Character->bUseControllerRotationYaw = true;
			Character->GetCharacterMovement()->bOrientRotationToMovement = false;
			Character->GetCharacterMovement()->bUseControllerDesiredRotation = false;
			break;
		}
	}
}

float UTurnInPlaceStatics::GetTurnInPlacePlayRate_ThreadSafe(const FTurnInPlaceAnimGraphData& AnimGraphData,
	bool bForceTurnRateMaxAngle, bool& bHasReachedMaxAngle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::GetTurnInPlacePlayRate_ThreadSafe);
	
	// Check if we've reached the max angle, or if we're forcing the max angle
	bHasReachedMaxAngle = bForceTurnRateMaxAngle;
	if (!bForceTurnRateMaxAngle)
	{
		if (AnimGraphData.bHasValidTurnAngles)
		{
			// Check if we're near the max angle
			bHasReachedMaxAngle |= FMath::IsNearlyEqual(FMath::Abs(AnimGraphData.TurnOffset), AnimGraphData.TurnAngles.MaxTurnAngle);
		}
	}

	// Rate changes, usually increases, when we're at the max angle to keep up with a player turning the camera (control rotation) quickly
	const float MaxAngleRate = bHasReachedMaxAngle ? AnimGraphData.AnimSet.PlayRateAtMaxAngle : 1.f;

	// Detect a change in direction and apply a rate change, so that if we're currently turning left and the player
	// wants to turn right, we speed up the turn rate so they can complete their old turn faster
	const bool bWantsTurnRight = AnimGraphData.TurnOffset > 0.f;
	const bool bDirectionChange = AnimGraphData.bIsTurning && bWantsTurnRight != AnimGraphData.bTurnRight;
	const float DirectionChangeRate = bDirectionChange ? AnimGraphData.AnimSet.PlayRateOnDirectionChange : 1.f;

	// Rates below 1.0 are not supported with this logic
	return FMath::Max(MaxAngleRate, DirectionChangeRate);
}

float UTurnInPlaceStatics::GetUpdatedTurnInPlaceAnimTime_ThreadSafe(const UAnimSequence* TurnAnimation, float CurrentAnimTime,
	float DeltaTime, float TurnPlayRate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::GetUpdatedTurnInPlaceAnimTime_ThreadSafe);
	
	if (!TurnAnimation)
	{
		return CurrentAnimTime;
	}

	const float Accumulate = DeltaTime * TurnPlayRate * TurnAnimation->RateScale;
	return FMath::Min(CurrentAnimTime + Accumulate, TurnAnimation->GetPlayLength());
}

float UTurnInPlaceStatics::GetAnimationSequencePlayRate(const UAnimSequenceBase* Animation)
{
	return Animation ? Animation->RateScale : 1.f;
}

FString UTurnInPlaceStatics::GetAnimationSequenceName(const UAnimSequenceBase* Animation)
{
	return Animation ? Animation->GetName() : "None";
}

void UTurnInPlaceStatics::DebugTurnInPlace(UObject* WorldContextObject, bool bDebug)
{
#if UE_ENABLE_DEBUG_DRAWING
	// Exec all debug commands
	const FString DebugState = bDebug ? TEXT(" 1") : TEXT(" 0");
	UKismetSystemLibrary::ExecuteConsoleCommand(WorldContextObject, TEXT("p.Turn.Debug.TurnOffset") + DebugState);
	UKismetSystemLibrary::ExecuteConsoleCommand(WorldContextObject, TEXT("p.Turn.Debug.TurnOffset.Arrow") + DebugState);
	UKismetSystemLibrary::ExecuteConsoleCommand(WorldContextObject, TEXT("p.Turn.Debug.ActorDirection.Arrow") + DebugState);
	UKismetSystemLibrary::ExecuteConsoleCommand(WorldContextObject, TEXT("p.Turn.Debug.ControlDirection.Arrow") + DebugState);
#endif
}

UAnimSequence* UTurnInPlaceStatics::GetTurnInPlaceAnimation(const FTurnInPlaceAnimCache& Cache, bool bRecovery)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::GetTurnInPlaceAnimation);
	
	const bool bTurnRight = bRecovery ? Cache.NodeData.bIsRecoveryTurningRight : Cache.NodeData.bIsTurningRight;
	const TArray<UAnimSequence*>& TurnAnimations = bTurnRight ? Cache.AnimData.AnimSet.RightTurns : Cache.AnimData.AnimSet.LeftTurns;
	return TurnAnimations.IsValidIndex(Cache.NodeData.StepSize) ? TurnAnimations[Cache.NodeData.StepSize] : nullptr;
}

UAnimSequence* UTurnInPlaceStatics::GetTurnInPlaceAnimation_AnimSet(const FTurnInPlaceAnimSet& AnimSet, const FTurnInPlaceGraphNodeData& NodeData, bool bRecovery)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::GetTurnInPlaceAnimation_NodeData);
	
	const bool bTurnRight = bRecovery ? NodeData.bIsRecoveryTurningRight : NodeData.bIsTurningRight;
	const TArray<UAnimSequence*>& TurnAnimations = bTurnRight ? AnimSet.RightTurns : AnimSet.LeftTurns;
	return TurnAnimations.IsValidIndex(NodeData.StepSize) ? TurnAnimations[NodeData.StepSize] : nullptr;
}

void UTurnInPlaceStatics::UpdateTurnInPlace(UTurnInPlace* TurnInPlace, float DeltaTime, FTurnInPlaceAnimCache& Cache, bool bIsStrafing, float& TurnOffset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::UpdateTurnInPlace_Entry);
	
	Cache.AnimData = FTurnInPlaceAnimGraphData();
	Cache.bCanUpdate = false;
	
	if (!TurnInPlace || !TurnInPlace->HasValidData())
	{
		TurnOffset = 0.f;
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::UpdateTurnInPlace);
	
	Cache.AnimData = TurnInPlace->UpdateAnimGraphData(DeltaTime);
	Cache.bCanUpdate = true;

	// The pseudo anim state needs to update here
	if (Cache.AnimData.bWantsPseudoAnimState)
	{
		ThreadSafeUpdateTurnInPlace_Internal(Cache.AnimData, Cache.bCanUpdate, bIsStrafing, Cache.AnimOutput);
	}

	TurnInPlace->PostUpdateAnimGraphData(DeltaTime, Cache.AnimData, Cache.AnimOutput);
	
	TurnOffset = TurnInPlace->GetTurnOffset();
}

void UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlace(const FTurnInPlaceAnimGraphData& AnimGraphData,
	bool bCanUpdateTurnInPlace, bool bIsStrafing, FTurnInPlaceAnimGraphOutput& Output)
{
	if (!AnimGraphData.bWantsPseudoAnimState)
	{
		ThreadSafeUpdateTurnInPlace_Internal(AnimGraphData, bCanUpdateTurnInPlace, bIsStrafing, Output);
	}
}

void UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlace_Internal(const FTurnInPlaceAnimGraphData& AnimGraphData,
	bool bCanUpdateTurnInPlace, bool bIsStrafing, FTurnInPlaceAnimGraphOutput& Output)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlace_Internal_Entry);
	
	if (!bCanUpdateTurnInPlace)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlace_Internal);

	// Turn anim graph properties
	Output.TurnOffset = AnimGraphData.TurnOffset;

	// Turn anim graph transitions.
	// bWantsTurnRecovery requires bWasTurningThisEntry: the turn-yaw-weight curve must have been observed driving
	// the turn at some point during this entry into TurnInPlace before the recovery transition is allowed to fire.
	// Without this latch, the entry frame sees stale (Idle-era) curves and immediately fires recovery, producing
	// the TurnInPlace<->TurnRecovery oscillation.
	//
	// Edge case the latch alone misses: if a turn plays to its end without the curve ever being observed >0,
	// bWasTurningThisEntry never latches and the state can never recover -- it sits parked at the end of the turn
	// animation forever. bTurnAnimReachedEnd is an oscillation-safe escape: it can only be true once the turn anim
	// has fully played out (never on the entry frame), so OR-ing it in breaks the stuck state without reintroducing
	// the entry-frame recovery oscillation.
	Output.bWantsToTurn = AnimGraphData.bWantsToTurn;
	Output.bWantsTurnRecovery = (AnimGraphData.bWasTurningThisEntry || AnimGraphData.bTurnAnimReachedEnd) &&
		!AnimGraphData.bIsTurning && !AnimGraphData.bAbortTurn;
	Output.bAbortTurn = AnimGraphData.bAbortTurn;

	// Locomotion anim graph transitions
	Output.bTransitionStartToCycleFromTurn = bIsStrafing && FMath::Abs(AnimGraphData.TurnOffset) > AnimGraphData.TurnAngles.MinTurnAngle;
	Output.bTransitionStopToIdleForTurn = AnimGraphData.bIsTurning || AnimGraphData.bWantsToTurn;

	// Play turn anim
	Output.bPlayTurnAnim = Output.bWantsToTurn && !AnimGraphData.bWantsPseudoAnimState;
}

FTurnInPlaceCurveValues UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceCurveValues(const UAnimInstance* AnimInstance, const FTurnInPlaceAnimGraphData& AnimGraphData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceCurveValues);
	
	FTurnInPlaceCurveValues CurveValues;

	// Turn anim graph curve values
	CurveValues.RemainingTurnYaw = AnimInstance->GetCurveValue(AnimGraphData.Settings.TurnYawCurveName);
	CurveValues.TurnYawWeight = AnimInstance->GetCurveValue(AnimGraphData.Settings.TurnWeightCurveName);
	CurveValues.PauseTurnInPlace = AnimInstance->GetCurveValue(AnimGraphData.Settings.PauseTurnInPlaceCurveName);
	CurveValues.LockTurnInPlace = AnimInstance->GetCurveValue(AnimGraphData.Settings.LockTurnInPlaceCurveName);

	return CurveValues;
}

void UTurnInPlaceStatics::ThreadSafeTurnInPlace(const UAnimInstance* AnimInstance, const UTurnInPlace* TurnInPlace, FTurnInPlaceAnimCache& Cache, bool bIsStrafing)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::ThreadSafeTurnInPlace);

	if (!AnimInstance)
	{
		return;
	}

	// Cache curve values in the worker thread at the same point where the anim state is determined. The game thread
	// can then request these values; querying them at a different time yields a different state and the anim state
	// becomes unreliable.
	Cache.CurveValues = ThreadSafeUpdateTurnInPlaceCurveValues(AnimInstance, Cache.AnimData);

	// Refresh bIsTurning and drive the bWasTurningThisEntry latch using the curves we just cached. UpdateTurnInPlace
	// runs on the game thread before this worker pass, so its bIsTurning reflects curves from two frames ago;
	// without this refresh the state machine downstream sees stale data and fires bWantsTurnRecovery on the entry
	// frame of TurnInPlace.
	if (TurnInPlace)
	{
		TurnInPlace->ThreadSafeRefreshAnimGraphData(Cache.AnimData, Cache.CurveValues, Cache.bWasTurningThisEntry);
	}
	else
	{
		Cache.AnimData.bWasTurningThisEntry = Cache.bWasTurningThisEntry;
	}

	// Carry the persistent "turn anim fully played" flag (maintained on the node data by the TurnInPlace state's
	// update function, which survives the per-frame rebuild of AnimGraphData) so the recovery transition can still
	// fire even if a turn finishes without ever registering as turning.
	Cache.AnimData.bTurnAnimReachedEnd = Cache.NodeData.bReachedAnimEnd;

	ThreadSafeUpdateTurnInPlace(Cache.AnimData, Cache.bCanUpdate, bIsStrafing, Cache.AnimOutput);
}

void UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceNode(FTurnInPlaceGraphNodeData& NodeData,
	const FTurnInPlaceAnimGraphData& AnimGraphData, const FTurnInPlaceAnimSet& AnimSet)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceNode);
	
	// Retain play rate at max angle for this current turn, if we ever reached it
	// This prevents micro jitters with mouse turning when it constantly re-enters max angle
	bool bHasReachedMaxAngle;
	NodeData.TurnPlayRate = GetTurnInPlacePlayRate_ThreadSafe(AnimGraphData, NodeData.bHasReachedMaxTurnAngle, bHasReachedMaxAngle);
	NodeData.bHasReachedMaxTurnAngle = AnimSet.bMaintainMaxAnglePlayRate && bHasReachedMaxAngle;
}

void UTurnInPlaceStatics::Setup_TurnIdle_Pose(FTurnInPlaceAnimCache& Cache)
{
	Cache.NodeData.bHasReachedMaxTurnAngle = false;
	Cache.NodeData.TurnPlayRate = 1.f;
}

void UTurnInPlaceStatics::Setup_TurnInPlace_Pose(FTurnInPlaceAnimCache& Cache)
{
	// This function always occurs prior to the sequence evaluator's OnBecomeRelevant
	// This is because it parses the nodes based on their links so we can be sure these are set prior to the evaluator running its logic

	Cache.NodeData.StepSize = Cache.AnimData.StepSize;
	Cache.NodeData.bIsTurningRight = Cache.AnimData.bTurnRight;

	// Reset the entry latch so the next worker pass can re-latch it from the just-entered turn anim's curves.
	// This is what prevents premature TurnInPlace -> TurnRecovery transitions caused by curve staleness.
	Cache.bWasTurningThisEntry = false;
}

void UTurnInPlaceStatics::Setup_TurnInPlace_Anim(FTurnInPlaceAnimCache& Cache)
{
	// We dumped the previous turn state due to inertialization, so using Set Sequence here will not cause the
	// pre-existing turn animation to snap when repeating this state rapidly

	Cache.NodeData.AnimStateTime = 0.f;
	Cache.NodeData.bHasReachedMaxTurnAngle = false;
	Cache.NodeData.bReachedAnimEnd = false;
	
	ThreadSafeUpdateTurnInPlaceNode(Cache.NodeData, Cache.AnimData, Cache.AnimData.AnimSet);
}

float UTurnInPlaceStatics::Update_TurnInPlace_Anim(FTurnInPlaceAnimCache& Cache, UAnimSequence* TurnAnim, float DeltaTime)
{
	if (!TurnAnim)
	{
		Cache.NodeData.AnimStateTime = 0.f;
		Cache.NodeData.bReachedAnimEnd = true;
		return 0.f;
	}
	
	// Even though we use Set Sequence in setup we need to allow changing mid-turn due to potential stance changes
	// updating the current turn animation (e.g. from stand turn to crouch turn)

	const float Time = GetUpdatedTurnInPlaceAnimTime_ThreadSafe(TurnAnim, Cache.NodeData.AnimStateTime,
		DeltaTime, Cache.NodeData.TurnPlayRate);

	Cache.NodeData.AnimStateTime = Time;
	
	// Flag when the turn animation has fully played out. This lets the recovery transition fire even if
	// bWasTurningThisEntry never latched (e.g. the turn-yaw-weight curve never registered this entry), which
	// otherwise leaves the character permanently stuck at the end of the turn animation.
	Cache.NodeData.bReachedAnimEnd = TurnAnim && (Time >= TurnAnim->GetPlayLength() - UE_KINDA_SMALL_NUMBER);

	ThreadSafeUpdateTurnInPlaceNode(Cache.NodeData, Cache.AnimData, Cache.AnimData.AnimSet);
	
	return Time;
}

void UTurnInPlaceStatics::Setup_TurnRecovery_Pose(FTurnInPlaceAnimCache& Cache)
{
	Cache.NodeData.bIsRecoveryTurningRight = Cache.NodeData.bIsTurningRight;
}
