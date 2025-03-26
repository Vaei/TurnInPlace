// Copyright (c) 2025 Jared Taylor


#include "TurnInPlaceModifier.h"

#include "Editor/AnimationBlueprintLibrary/Public/AnimationBlueprintLibrary.h"
#include "System/TurnInPlaceVersioning.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlaceModifier)

FTransform UTurnInPlaceModifier::ExtractBoneTransform(const UAnimSequence* Animation,
	const FBoneContainer& BoneContainer, FCompactPoseBoneIndex CompactPoseBoneIndex, float Time, bool bComponentSpace)
{
	ensureAlways(!Animation->bForceRootLock);

	FCompactPose Pose;
	Pose.SetBoneContainer(&BoneContainer);

	FBlendedCurve Curve;
	Curve.InitFrom(BoneContainer);

	const FAnimExtractContext Context(static_cast<double>(Time), false);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData AnimationPoseData(Pose, Curve, Attributes);

	Animation->GetBonePose(AnimationPoseData, Context, true);

	check(Pose.IsValidIndex(CompactPoseBoneIndex));

	if (bComponentSpace)
	{
		FCSPose<FCompactPose> ComponentSpacePose;
		ComponentSpacePose.InitPose(Pose);
		return ComponentSpacePose.GetComponentSpaceTransform(CompactPoseBoneIndex);
	}
	else
	{
		return Pose[CompactPoseBoneIndex];
	}
}

void UTurnInPlaceModifier::OnApply_Implementation(UAnimSequence* Animation)
{
	if (Animation == nullptr)
	{
		UE_LOG(LogAnimation, Error, TEXT("MotionExtractorModifier failed. Reason: Invalid Animation"));
		return;
	}

	USkeleton* Skeleton = Animation->GetSkeleton();
	if (Skeleton == nullptr)
	{
		UE_LOG(LogAnimation, Error, TEXT("MotionExtractorModifier failed. Reason: Animation with invalid Skeleton. Animation: %s"),
			*GetNameSafe(Animation));
		return;
	}

	const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogAnimation, Error, TEXT("MotionExtractorModifier failed. Reason: Invalid Bone Index. BoneName: %s Animation: %s Skeleton: %s"),
			*BoneName.ToString(), *GetNameSafe(Animation), *GetNameSafe(Skeleton));
		return;
	}

	FMemMark Mark(FMemStack::Get());

	TGuardValue<bool> ForceRootLockGuard(Animation->bForceRootLock, false);

	TArray<FBoneIndexType> RequiredBones;
	RequiredBones.Add(BoneIndex);
	Skeleton->GetReferenceSkeleton().EnsureParentsExistAndSort(RequiredBones);

#if UE_5_03_OR_LATER
	UE::Anim::FCurveFilterSettings FilterSettings;
	const FBoneContainer BoneContainer(RequiredBones, FilterSettings, *Skeleton);
#else
	const FBoneContainer BoneContainer(RequiredBones, false, *Skeleton);
#endif
	const FCompactPoseBoneIndex CompactPoseBoneIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));

	static constexpr bool bComponentSpace = true;
	const FTransform LastFrameBoneTransform = ExtractBoneTransform(Animation, BoneContainer, CompactPoseBoneIndex, Animation->GetPlayLength(), bComponentSpace);

	const float AnimLength = Animation->GetPlayLength();
	const float SampleInterval = 1.f / SampleRate;

	float Time = 0.f;
	int32 SampleIndex = 0;

	TArray<FRichCurveKey> CurveKeys;
	TArray<FRichCurveKey> WeightCurveKeys;

	// First weight key
	bool bPlacedWeightEndKey = false;
	{
		FRichCurveKey& Key = WeightCurveKeys.AddDefaulted_GetRef();
		Key.Time = 0.f;
		Key.Value = 1.f;
		Key.InterpMode = RCIM_Constant;
	}

	while (Time < AnimLength)
	{
		Time = FMath::Clamp(SampleIndex * SampleInterval, 0.f, AnimLength);
		const float NextTime = FMath::Clamp((SampleIndex + 1) * SampleInterval, 0.f, AnimLength);
		SampleIndex++;

		FTransform BoneTransform = ExtractBoneTransform(Animation, BoneContainer, CompactPoseBoneIndex, Time, bComponentSpace);

		static constexpr bool bRelativeToFirstFrame = true;
		if (bRelativeToFirstFrame)
		{
			BoneTransform = BoneTransform.GetRelativeTransform(LastFrameBoneTransform).Inverse();
		}
		
		const float Value = BoneTransform.GetRotation().Rotator().Yaw;
		const float Pct = Time / AnimLength;
		const bool bMaxPct = Pct >= (1.f - MaxWeightOffsetPct);
		const bool bMaxBlendTime = NextTime >= (AnimLength - GraphTransitionBlendTime);  // Start the frame before the transition
		if (!bPlacedWeightEndKey && (FMath::IsNearlyZero(Value, 0.1f) || bMaxBlendTime || bMaxPct))
		{
			// Guess where the turn actually ends
			bPlacedWeightEndKey = true;
			FRichCurveKey& Key = WeightCurveKeys.AddDefaulted_GetRef();
			Key.Time = Time;
			Key.Value = 0.f;
			Key.InterpMode = RCIM_Constant;
		}

		FRichCurveKey& Key = CurveKeys.AddDefaulted_GetRef();
		Key.Time = Time;
		Key.Value = Value;
	}

	// Remove curve if it exists
	if (UAnimationBlueprintLibrary::DoesCurveExist(Animation, TurnYawCurveName, ERawCurveTrackTypes::RCT_Float))
	{
		UAnimationBlueprintLibrary::RemoveCurve(Animation, TurnYawCurveName);
	}

	if (UAnimationBlueprintLibrary::DoesCurveExist(Animation, TurnWeightCurveName, ERawCurveTrackTypes::RCT_Float))
	{
		UAnimationBlueprintLibrary::RemoveCurve(Animation, TurnWeightCurveName);
	}

	IAnimationDataController& Controller = Animation->GetController();
	// Remaining Turn Yaw
	{
		const FAnimationCurveIdentifier CurveId = UAnimationCurveIdentifierExtensions::GetCurveIdentifier(Animation->GetSkeleton(), TurnYawCurveName, ERawCurveTrackTypes::RCT_Float);
		if (CurveKeys.Num() && Controller.AddCurve(CurveId))
		{
			Controller.SetCurveKeys(CurveId, CurveKeys);
		}
	}

	// Weight
	const FAnimationCurveIdentifier CurveId = UAnimationCurveIdentifierExtensions::GetCurveIdentifier(Animation->GetSkeleton(), TurnWeightCurveName, ERawCurveTrackTypes::RCT_Float);
	if (WeightCurveKeys.Num() && Controller.AddCurve(CurveId))
	{
		Controller.SetCurveKeys(CurveId, WeightCurveKeys);
	}
}

void UTurnInPlaceModifier::OnRevert_Implementation(UAnimSequence* Animation)
{
	static constexpr bool bRemoveCurveOnRevert = true;
	if (bRemoveCurveOnRevert)
	{
		static constexpr bool bRemoveNameFromSkeleton = false;
		UAnimationBlueprintLibrary::RemoveCurve(Animation, TurnYawCurveName, bRemoveNameFromSkeleton);
		UAnimationBlueprintLibrary::RemoveCurve(Animation, TurnWeightCurveName, bRemoveNameFromSkeleton);
	}
}
