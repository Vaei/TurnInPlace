// Copyright (c) 2025 Jared Taylor

#pragma once

/*
 * This contains the same Anim Graph functions used in the Anim Graph Blueprint, but in C++
 * Its purpose is to be conveniently diffed and show any changes to the Anim Graph logic in a more readable way
 * You should NOT include this file and copy what you need out, or reproduce the changes in your Anim Graph Blueprint
 * 
 * SETUP_ functions go in OnBecomeRelevant event
 * UPDATE_ functions go in the OnUpdate event
 * _POSE functions go on the OutputAnimationPose node
 * _ANIM functions go on the SequencePlayer or SequenceEvaluator nodes

*******************************************

MyAnimInstance.h

UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
float TurnOffset = 0.f;

UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Turn)
FTurnInPlaceAnimCache TurnCache;

UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Transition)
bool bStartToRun = false;

UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Transition)
bool bStopToIdle = false;

*******************************************

void UMyAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	UTurnInPlaceStatics::UpdateTurnInPlace(TurnInPlace, DeltaTime, TurnCache, bIsStrafing, TurnOffset);
}

void UMyAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaTime)
{
	UTurnInPlaceStatics::ThreadSafeTurnInPlace(this, TurnInPlace, TurnCache, bIsStrafing);
	
	// Recommended transition logic (as well as whatever you already have)
	bStartToRun = FMath::Abs(TurnOffset) > TurnCache.AnimData.TurnAngles.MinTurnAngle;
	bStopToIdle = bStanceChanged || TurnCache.AnimData.bIsTurning || TurnCache.AnimData.bWantsToTurn;
}

FTurnInPlaceCurveValues UMyAnimInstance::GetTurnInPlaceCurveValues_Implementation() const
{
	return TurnCache.CurveValues;
}

*******************************************

static UAnimSequence* GetTurnInPlaceAnim(bool bRecovery)
{
	return UTurnInPlaceStatics::GetTurnInPlaceAnimation(TurnCache, bRecovery);
}

void UMyAnimLayer::Setup_Idle_Pose(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	if (!CanUpdateAnimGraph()) { return; }

	UTurnInPlaceStatics::Setup_TurnIdle_Pose(TurnCache);
}

void UMyAnimLayer::Setup_TurnInPlace_Pose(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	UTurnInPlaceStatics::Setup_TurnInPlace_Pose(TurnCache);
}

void UMyAnimLayer::Setup_TurnInPlace_Anim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	UTurnInPlaceStatics::Setup_TurnInPlace_Anim(TurnCache);

	constexpr bool bRecovery = false;
	UAnimSequence* A = GetTurnInPlaceAnim(bRecovery);

	const auto& R = NodeType<FSequenceEvaluatorReference>(Node);
	USequenceEvaluatorLibrary::SetSequence(R, A);
	USequenceEvaluatorLibrary::SetExplicitTime(R, 0.f);
}

void UMyAnimLayer::Update_TurnInPlace_Anim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	// Even though we use Set Sequence in setup we need to allow changing mid-turn due to potential stance changes
	// updating the current turn animation (e.g. from stand turn to crouch turn)

	constexpr bool bRecovery = false;
	UAnimSequence* A = GetTurnInPlaceAnim(bRecovery);

	const auto& R = NodeType<FSequenceEvaluatorReference>(Node);
	USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(Context, R, A, 0.2f);

	// The returned time MUST be applied to the evaluator, or the turn animation never advances and the character
	// will not turn at all
	const float Time = UTurnInPlaceStatics::Update_TurnInPlace_Anim(TurnCache, A, Context.GetContext()->GetDeltaTime());
	USequenceEvaluatorLibrary::SetExplicitTime(R, Time);
}

void UMyAnimLayer::Setup_TurnRecovery_Pose(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	UTurnInPlaceStatics::Setup_TurnRecovery_Pose(TurnCache);
}

void UMyAnimLayer::Update_TurnRecovery_Anim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	constexpr bool bRecovery = true;
	UAnimSequence* A = GetTurnInPlaceAnim(bRecovery);
	
	USequencePlayerLibrary::SetSequenceWithInertialBlending(Context, NodeType<FSequencePlayerReference>(Node), A, 0.2f);
}

*/