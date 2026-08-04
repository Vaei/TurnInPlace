// Copyright (c) 2025 Jared Taylor


#include "TurnInPlace.h"

#include "GameplayTagContainer.h"
#include "TurnInPlaceAnimInterface.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

#if WITH_EDITOR
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif

#if WITH_SIMPLE_ANIMATION && UE_ENABLE_DEBUG_DRAWING
#include "SimpleAnimLib.h"
#endif

#include "TurnInPlaceStatics.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TurnInPlace)

DEFINE_LOG_CATEGORY_STATIC(LogTurnInPlace, Log, All);

#define LOCTEXT_NAMESPACE "TurnInPlaceComponent"

namespace TurnInPlaceLocal
{
	// Unit heading vector of a rotation within the plane perpendicular to UpAxis.
	//
	// The forward vector is the natural heading, but when the rotation looks almost straight along UpAxis its
	// projection onto the plane collapses to a near-zero vector whose direction is wildly sensitive to UpAxis. Re-
	// deriving the heading from the right vector in that regime is stable: the right vector stays in-plane even when
	// forward is parallel to UpAxis. We pick whichever basis vector projects more strongly into the plane, which is
	// continuous across the crossover (for a roll-free rotation both give the same heading) and well defined at every
	// pitch, since forward and right can never both be parallel to UpAxis. (Fwd = Right x Up.)
	static FVector HeadingInPlane(const FRotator& Rot, const FVector& UpAxis)
	{
		const FQuat Q = Rot.Quaternion();
		const FVector FwdProj = FVector::VectorPlaneProject(Q.GetForwardVector(), UpAxis);
		const FVector RightProj = FVector::VectorPlaneProject(Q.GetRightVector(), UpAxis);
		if (FwdProj.SizeSquared() >= RightProj.SizeSquared())
		{
			return FwdProj.GetSafeNormal();
		}
		return FVector::CrossProduct(RightProj, UpAxis).GetSafeNormal();
	}

	// Yaw delta from From to To, measured as a heading difference about UpAxis.
	//
	// Forward = (cosP*cosY, cosP*sinY, sinP) is independent of roll, and its world-horizontal heading is exactly Y,
	// so when neither rotation has roll the FRotator yaw component already is the heading (pitch is irrelevant) and a
	// plain yaw subtraction is exact. We only fall back to projection when roll is present, where world-space roll
	// composition makes the stored yaw diverge from the heading. Gating on roll keeps flat-ground play on the original
	// cheap math (and bit-identical for replicated TurnOffset). We deliberately do NOT gate on pitch: control rotation
	// routinely carries pitch.
	//
	// UpAxis lets the heading be measured about a custom-gravity up instead of world Z. When UpAxis is world up the
	// projection reduces to the original world-horizontal math. HeadingInPlace keeps the measurement stable even when
	// the rotation looks nearly straight up/down the up axis, which otherwise made the projected heading swing as the
	// up axis moved and spuriously drive turn-in-place.
	static float ComputeYawDeltaAroundUp(const FRotator& From, const FRotator& To, const FVector& UpAxis = FVector::UpVector)
	{
		const bool bWorldUp = UpAxis.Equals(FVector::UpVector);
		if (bWorldUp && FMath::IsNearlyZero(From.Roll) && FMath::IsNearlyZero(To.Roll))
		{
			return FRotator::NormalizeAxis(To.Yaw - From.Yaw);
		}

		const FVector FromH = HeadingInPlane(From, UpAxis);
		const FVector ToH = HeadingInPlane(To, UpAxis);
		if (FromH.IsNearlyZero() || ToH.IsNearlyZero())
		{
			// Unreachable in practice (forward and right can't both be parallel to UpAxis); kept as a safety net.
			return FRotator::NormalizeAxis(To.Yaw - From.Yaw);
		}
		const float Cross = (FromH ^ ToH) | UpAxis;
		const float Dot = FromH | ToH;
		return FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}
}

namespace TurnInPlaceCvars
{
#if UE_ENABLE_DEBUG_DRAWING
	static bool bDebugTurnOffset = false;
	static FAutoConsoleVariableRef CVarDebugTurnOffset(
		TEXT("p.Turn.Debug.TurnOffset"),
		bDebugTurnOffset,
		TEXT("Draw TurnOffset on screen"),
		ECVF_Default);

	static bool bDebugTurnOffsetArrow = false;
	static FAutoConsoleVariableRef CVarDebugTurnOffsetArrow(
		TEXT("p.Turn.Debug.TurnOffset.Arrow"),
		bDebugTurnOffsetArrow,
		TEXT("Draw GREEN debug arrow showing the direction of the turn offset"),
		ECVF_Default);

	static bool bDebugActorDirectionArrow = false;
	static FAutoConsoleVariableRef CVarDebugActorDirectionArrow(
		TEXT("p.Turn.Debug.ActorDirection.Arrow"),
		bDebugActorDirectionArrow,
		TEXT("Draw PINK debug arrow showing the direction the actor rotation is facing"),
		ECVF_Default);

	static bool bDebugControlDirectionArrow = false;
	static FAutoConsoleVariableRef CVarDebugControlDirectionArrow(
		TEXT("p.Turn.Debug.ControlDirection.Arrow"),
		bDebugControlDirectionArrow,
		TEXT("Draw BLACK debug arrow showing the direction the control rotation is facing"),
		ECVF_Default);
#endif

#if !UE_BUILD_SHIPPING
	static int32 OverrideTurnInPlace = 0;
	static FAutoConsoleVariableRef CVarOverrideTurnInPlace(
		TEXT("p.Turn.Override"),
		OverrideTurnInPlace,
		TEXT("Override Turn In Place. 0 = Default, 1 = Force Enabled, 2 = Force Locked, 3 = Force Paused (Disabled)"),
		ECVF_Cheat);
#endif
}

UTurnInPlace::UTurnInPlace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsValidAnimInstance(false)
	, bWarnIfAnimInterfaceNotImplemented(true)
	, bHasWarned(false)
{
	// We don't need to tick
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	
	// Replicate the turn offset to simulated proxies
	SetIsReplicatedByDefault(true);
}

void UTurnInPlace::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// Push Model
	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;
	SharedParams.Condition = COND_SimulatedOnly;

	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, SimulatedTurnOffset, SharedParams);
}

ENetRole UTurnInPlace::GetLocalRole() const
{
	return IsValid(GetOwner()) ? GetOwner()->GetLocalRole() : ROLE_None;
}

bool UTurnInPlace::HasAuthority() const
{
	return IsValid(GetOwner()) ? GetOwner()->HasAuthority() : false;
}

void UTurnInPlace::CompressSimulatedTurnOffset(float LastTurnOffset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::CompressSimulatedTurnOffset);
	
	// Compress result and replicate turn offset to simulated proxy
	if (HasAuthority() && GetNetMode() != NM_Standalone)
	{
		if (HasTurnOffsetChanged(GetTurnOffset(), LastTurnOffset))
		{
			SimulatedTurnOffset.Compress(GetTurnOffset());
			MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, SimulatedTurnOffset, this);
		}
	}
}

void UTurnInPlace::OnRep_SimulatedTurnOffset()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::OnRep_SimulatedTurnOffset);
	
	// Decompress the replicated value from short to float, and apply it to the TurnInPlace component
	// This keeps simulated proxies in sync with the server and allows them to turn in place
	if (GetLocalRole() == ROLE_SimulatedProxy && HasValidData())
	{
		TurnData.TurnOffset = SimulatedTurnOffset.Decompress();
	}
}

void UTurnInPlace::OnRegister()
{
	Super::OnRegister();
	
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		CacheUpdatedCharacter();
	}
}

void UTurnInPlace::InitializeComponent()
{
	Super::InitializeComponent();
	CacheUpdatedCharacter();
}

void UTurnInPlace::CacheUpdatedCharacter_Implementation()
{
	PawnOwner = IsValid(GetOwner()) ? Cast<APawn>(GetOwner()) : nullptr;
	MaybeCharacter = IsValid(GetOwner()) ? Cast<ACharacter>(GetOwner()) : nullptr;
}

void UTurnInPlace::BeginPlay()
{
	Super::BeginPlay();

	// Bind to the Mesh event to detect when the AnimInstance changes so we can recache it and check if it implements UTurnInPlaceAnimInterface
	if (ensureAlways(IsValid(GetOwner())))
	{
		if (GetMesh())
		{
			if (GetMesh()->OnAnimInitialized.IsBound())
			{
				GetMesh()->OnAnimInitialized.RemoveDynamic(this, &ThisClass::OnAnimInstanceChanged);
			}
			GetMesh()->OnAnimInitialized.AddDynamic(this, &ThisClass::OnAnimInstanceChanged);
			OnAnimInstanceChanged();
		}
	}
}

void UTurnInPlace::DestroyComponent(bool bPromoteChildren)
{
	// Unbind from the Mesh's AnimInstance event
	if (GetMesh())
	{
		if (GetMesh()->OnAnimInitialized.IsBound())
		{
			GetMesh()->OnAnimInitialized.RemoveDynamic(this, &ThisClass::OnAnimInstanceChanged);
		}
	}
	
	Super::DestroyComponent(bPromoteChildren);
}

void UTurnInPlace::OnAnimInstanceChanged()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::OnAnimInstanceChanged);
	
	// Cache the AnimInstance and check if it implements UTurnInPlaceAnimInterface
	AnimInstance = GetMesh()->GetAnimInstance();
	bIsValidAnimInstance = false;
	if (IsValid(AnimInstance))
	{
		// Check if the AnimInstance implements the TurnInPlaceAnimInterface and cache the result so we don't have to check every frame
		bIsValidAnimInstance = AnimInstance->Implements<UTurnInPlaceAnimInterface>();
		if (!bIsValidAnimInstance && bWarnIfAnimInterfaceNotImplemented && !bHasWarned)
		{
			// Log a warning if the AnimInstance does not implement the TurnInPlaceAnimInterface
			bHasWarned = true;
			const FText ErrorMsg = FText::Format(
				LOCTEXT("InvalidAnimInstance", "The anim instance {0} assigned to {1} on {2} does not implement the TurnInPlaceAnimInterface."),
				FText::FromString(AnimInstance->GetClass()->GetName()), FText::FromString(GetMesh()->GetName()), FText::FromString(GetName()));
#if WITH_EDITOR
			// Show a notification in the editor
			FNotificationInfo Info(FText::FromString("Invalid Turn In Place Setup. See Message Log."));
			Info.ExpireDuration = 6.f;
			FSlateNotificationManager::Get().AddNotification(Info);

			// Log the error to message log
			FMessageLog("PIE").Error(ErrorMsg);
#else
			// Log the error to the output log
			UE_LOG(LogTurnInPlace, Error, TEXT("%s"), *ErrorMsg.ToString());
#endif
		}
	}
}

bool UTurnInPlace::IsTurningInPlace() const
{
	// We are turning in place if the weight curve is not 0
	return HasValidData() && !FMath::IsNearlyZero(GetCurveValues().TurnYawWeight, KINDA_SMALL_NUMBER);
}

USkeletalMeshComponent* UTurnInPlace::GetMesh_Implementation() const
{
	if (MaybeCharacter)
	{
		return MaybeCharacter->GetMesh();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

bool UTurnInPlace::IsCharacterStationary() const
{
	return GetOwner()->GetVelocity().IsNearlyZero();
}

UAnimMontage* UTurnInPlace::GetCurrentNetworkRootMotionMontage() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetCurrentNetworkRootMotionMontage);
	
	// Check if the character is playing a networked root motion montage
	if (bIsValidAnimInstance && IsPlayingNetworkedRootMotionMontage())
	{
		// Get the root motion montage instance and return the montage
		if (const FAnimMontageInstance* MontageInstance = AnimInstance->GetRootMotionMontageInstance())
		{
			return MontageInstance->Montage;
		}
	}
	return nullptr;
}

ETurnInPlaceOverride UTurnInPlace::GetOverrideForMontage_Implementation(const UAnimMontage* Montage) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetOverrideForMontage);
	
	// Allow overriding per-montage
	if (HasValidData() && Montage)
	{
		FTurnInPlaceParams Params = GetParams();
		if (const ETurnInPlaceOverride* Override = Params.MontageHandling.MontageOverrides.Find(Montage))
		{
#if WITH_EDITOR
			if (*Override == ETurnInPlaceOverride::Default)
			{
				FMessageLog("PIE").Warning(FText::Format(
					LOCTEXT("MontageOverrideDefault", "Montage {0} has an override of Default. AnimInstance {1}. Owner {2}. This will be ignored."),
					FText::FromString(Montage->GetName()), FText::FromString(AnimInstance->GetName()), FText::FromString(GetOwner()->GetName())));
			}
#endif
			return *Override;
		}
	}
	return ETurnInPlaceOverride::Default;
}

AController* UTurnInPlace::GetController_Implementation() const
{
	return IsValid(MaybeCharacter) ? MaybeCharacter->GetController() : nullptr;
}

bool UTurnInPlace::IsPlayingNetworkedRootMotionMontage_Implementation() const
{
	return IsValid(MaybeCharacter) && MaybeCharacter->IsPlayingNetworkedRootMotionMontage();
}

bool UTurnInPlace::ShouldIgnoreRootMotionMontage_Implementation(const UAnimMontage* Montage) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::ShouldIgnoreRootMotionMontage);
	
	if (!HasValidData())
	{
		return false;
	}

	FTurnInPlaceParams Params = GetParams();

	// Check if the montage itself is ignored
	if (Params.MontageHandling.IgnoreMontages.Contains(Montage))
	{
		return true;
	}

	// We generally don't want to consider any montages that are additive as playing a montage
	if (Params.MontageHandling.bIgnoreAdditiveMontages && Montage->IsValidAdditive())
	{
		return true;
	}

	// Check if any montage anim tracks ignore this slot
	for (const FName& Slot : Params.MontageHandling.IgnoreMontageSlots)
	{
		if (Montage->IsValidSlot(Slot))
		{
			return true;
		}
	}
	
	return false;
}

FVector UTurnInPlace::GetDebugDrawArrowLocation_Implementation(bool& bIsValidLocation) const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCapsuleComponent())
	{
		bIsValidLocation = false;
		return FVector::ZeroVector;
	}

	bIsValidLocation = true;
	
	const float HalfHeight = MaybeCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector& ActorLocation = MaybeCharacter->GetActorLocation();
	return ActorLocation - (FVector::UpVector * HalfHeight);
#else
	return FVector::ZeroVector;
#endif
}

ETurnInPlaceOverride UTurnInPlace::OverrideTurnInPlace_Implementation() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::OverrideTurnInPlace);
	
#if !UE_BUILD_SHIPPING
	if (TurnInPlaceCvars::OverrideTurnInPlace > 0)
	{
		switch (TurnInPlaceCvars::OverrideTurnInPlace)
		{
		case 1: return ETurnInPlaceOverride::ForceEnabled;
		case 2: return ETurnInPlaceOverride::ForceLocked;
		case 3: return ETurnInPlaceOverride::ForcePaused;
		default: break;
		}
	}
#endif

	// Curve values are used to determine if we should pause or lock turn in place.
	// Use a one-sided threshold (>= 0.95) instead of IsNearlyEqual(.., 1.0, 0.05) so inertialization or curve
	// overshoot above 1.05 doesn't disengage the override for a single frame and pop the state machine.
	const FTurnInPlaceCurveValues& CurveValues = GetCurveValues();

	if (CurveValues.PauseTurnInPlace >= 0.95f)
	{
		return ETurnInPlaceOverride::ForcePaused;
	}

	if (CurveValues.LockTurnInPlace >= 0.95f)
	{
		return ETurnInPlaceOverride::ForceLocked;
	}

	// We want to pause turn in place when using root motion montages
	if (const UAnimMontage* Montage = GetCurrentNetworkRootMotionMontage())
	{
		// But we don't want to pause turn in place if the montage is ignored by our current params
		if (!ShouldIgnoreRootMotionMontage(Montage))
		{
			return ETurnInPlaceOverride::ForcePaused;
		}
	}

	return ETurnInPlaceOverride::Default;
}

bool UTurnInPlace::GetCustomGravity(FVector& GravityUp, FQuat& WorldToGravity, FQuat& GravityToWorld) const
{
	if (MaybeCharacter)
	{
		if (const UCharacterMovementComponent* CMC = MaybeCharacter->GetCharacterMovement())
		{
			if (CMC->HasCustomGravity())
			{
				const FVector Up = (-CMC->GetGravityDirection()).GetSafeNormal();
				if (!Up.IsNearlyZero())
				{
					GravityUp = Up;
					WorldToGravity = CMC->GetWorldToGravityTransform();
					GravityToWorld = CMC->GetGravityToWorldTransform();
					return true;
				}
			}
		}
	}
	
	return false;
}

FGameplayTag UTurnInPlace::GetTurnModeTag_Implementation() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetTurnModeTag);
	
	// Determine the turn mode tag based on the character's movement settings
	const bool bIsStrafing = MaybeCharacter && MaybeCharacter->GetCharacterMovement() && !MaybeCharacter->GetCharacterMovement()->bOrientRotationToMovement;
	return bIsStrafing ? FTurnInPlaceTags::TurnMode_Strafe : FTurnInPlaceTags::TurnMode_Movement;
}

ETurnInPlaceEnabledState UTurnInPlace::GetEnabledState(const FTurnInPlaceParams& Params) const
{
	if (!HasValidData())
	{
		return ETurnInPlaceEnabledState::Locked;
	}

	// Determine the enabled state of turn in place
	// This allows us to lock or pause turn in place, or force it to be enabled based on runtime conditions
	const ETurnInPlaceEnabledState State = Params.State;
	const ETurnInPlaceOverride OverrideState = OverrideTurnInPlace();
	switch (OverrideState)
	{
	case ETurnInPlaceOverride::Default: return State;
	case ETurnInPlaceOverride::ForceEnabled: return ETurnInPlaceEnabledState::Enabled;
	case ETurnInPlaceOverride::ForceLocked: return ETurnInPlaceEnabledState::Locked;
	case ETurnInPlaceOverride::ForcePaused: return ETurnInPlaceEnabledState::Paused;
	default: return State;
	}
}

FTurnInPlaceAnimSet UTurnInPlace::GetTurnInPlaceAnimSet() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetTurnInPlaceAnimSet);
	
	return ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceAnimSet(AnimInstance);
}

FTurnInPlaceParams UTurnInPlace::GetParams() const
{
	if (!HasValidData())
	{
		return {};
	}

	// Get the current turn in place parameters from the animation blueprint
	FTurnInPlaceAnimSet AnimSet = GetTurnInPlaceAnimSet();
	return AnimSet.Params;
}

FTurnInPlaceCurveValues UTurnInPlace::GetCurveValues() const
{
	if (!HasValidData())
	{
		return {};
	}

	// Dedicated server might want to use pseudo anim state instead of playing actual animations
	if (WantsPseudoAnimState())
	{
		if (PseudoAnim)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetCurveValues::PseudoAnim);
			
			const float Yaw = PseudoAnim->EvaluateCurveData(Settings.TurnYawCurveName, PseudoNodeData.AnimStateTime);
			const float Weight = PseudoAnim->EvaluateCurveData(Settings.TurnWeightCurveName, PseudoNodeData.AnimStateTime);
			const float Pause = PseudoAnim->EvaluateCurveData(Settings.PauseTurnInPlaceCurveName, PseudoNodeData.AnimStateTime);
			const float Lock = PseudoAnim->EvaluateCurveData(Settings.LockTurnInPlaceCurveName, PseudoNodeData.AnimStateTime);
			return { Yaw, Weight, Pause, Lock };
		}
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetCurveValues);

	// Get the current turn in place curve values from the animation blueprint
	return ITurnInPlaceAnimInterface::Execute_GetTurnInPlaceCurveValues(AnimInstance);
}

bool UTurnInPlace::WantsPseudoAnimState() const
{
	return GetNetMode() == NM_DedicatedServer && DedicatedServerAnimUpdateMode == ETurnAnimUpdateMode::Pseudo;
}

bool UTurnInPlace::HasValidData() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::HasValidData);
	
	// We need a valid AnimInstance and Character to proceed, and the anim instance must implement the TurnInPlaceAnimInterface
	return bIsValidAnimInstance && IsValid(GetOwner()) && !GetOwner()->IsPendingKillPending();
}

ETurnMethod UTurnInPlace::GetTurnMethod() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::GetTurnMethod);
	
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCharacterMovement())
	{
		return ETurnMethod::None;
	}

	// ACharacter::FaceRotation handles turn in place when bOrientRotationToMovement is false, and we orient to control rotation
	// This is an instant snapping turn that rotates to control rotation
	if (!MaybeCharacter->GetCharacterMovement()->bOrientRotationToMovement)
	{
		if (MaybeCharacter->bUseControllerRotationPitch || MaybeCharacter->bUseControllerRotationYaw || MaybeCharacter->bUseControllerRotationRoll)
		{
			return ETurnMethod::FaceRotation;
		}
	}

	// UCharacterMovementComponent::PhysicsRotation handles orienting rotation to movement or controller desired rotation
	// This is a smooth rotation that interpolates to the desired rotation
	return ETurnMethod::PhysicsRotation;
}

bool UTurnInPlace::HasTurnOffsetChanged(float CurrentValue, float LastValue)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::HasTurnOffsetChanged);
	
	const FQuat LastTurnQuat = FRotator(0.f, LastValue, 0.f).Quaternion();
	const FQuat CurrentTurnQuat = FRotator(0.f, CurrentValue, 0.f).Quaternion();
	return !CurrentTurnQuat.Equals(LastTurnQuat, TURN_ROTATOR_TOLERANCE);
}

void UTurnInPlace::SimulateTurnInPlace()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::SimulateTurnInPlace);
	
	if (bSimulateAnimationCurves && HasValidData() && GetOwnerRole() == ROLE_SimulatedProxy && IsCharacterStationary())
	{
		TurnInPlace(FRotator::ZeroRotator, FRotator::ZeroRotator, true);
	}
}

void UTurnInPlace::TurnInPlace(const FRotator& CurrentRotation, const FRotator& DesiredRotation, bool bClientSimulation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::TurnInPlace);

	// Determine the correct params to use
	const FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);

	/**
	 * Measure and apply yaw about the gravity up axis. With default (world-down) gravity the transforms are identity
	 * and the math reduces to plain world-horizontal yaw.
	 */
	FVector GravityUp = FVector::UpVector;
	FQuat WorldToGravity = FQuat::Identity;
	FQuat GravityToWorld = FQuat::Identity;
	GetCustomGravity(GravityUp, WorldToGravity, GravityToWorld);

	/**
	 * Sets the actor vertical in gravity space at BaseWorldRotation's gravity-space heading plus an optional yaw delta
	 * (degrees) about the gravity up axis. Re-verticalling every frame is what keeps the body tilted to the base while
	 * standing, since the engine PhysicsRotation never runs in the FaceRotation path. Gravity is the only thing allowed
	 * to tilt the actor, so this runs unconditionally: under default gravity it zeroes pitch/roll, which is what returns
	 * the body to upright once a tilted base is left behind.
	 */
	auto ApplyGravityVertical = [&](const FRotator& BaseWorldRotation, float AddYawDeg)
	{
		const float GravYaw = (GravityToWorld * BaseWorldRotation.Quaternion()).Rotator().Yaw + AddYawDeg;
		GetOwner()->SetActorRotation((WorldToGravity * FRotator(0.f, GravYaw, 0.f).Quaternion()).Rotator());
	};

	// Turn in place is locked, we can't turn - but still keep the body aligned to gravity
	const bool bEnabled = State != ETurnInPlaceEnabledState::Locked;
	if (!bEnabled)
	{
		/**
		 * A simulated proxy must never discard TurnOffset: it is replicated, and replication only resends the
		 * property when the server's value changes. The locked state here is evaluated from local, non-authoritative
		 * signals (this proxy's own lock curve, its montage state, gameplay tags that replicate on their own
		 * schedule), so a lock the server never saw would strand the proxy at zero until the server's offset
		 * happens to move again, which it does not while the player holds a steady offset.
		 */
		if (bClientSimulation)
		{
			return;
		}

		TurnData = {};
		ApplyGravityVertical(CurrentRotation, 0.f);
		return;
	}

	if (!bClientSimulation)
	{
		// Reset it here, because we are not appending, and this accounts for velocity being applied (no turn in place)
		TurnData.TurnOffset = 0.f;
		TurnData.InterpOutAlpha = 0.f;

		// If turn in place is paused, we can't accumulate any turn offset
		if (State != ETurnInPlaceEnabledState::Paused)
		{
			// Yaw delta measured about the gravity up axis (robust to roll/pitch carried into the actor's
			// rotation) rather than the raw FRotator yaw component, which suffers Euler-decomposition cross-talk.
			TurnData.TurnOffset = TurnInPlaceLocal::ComputeYawDeltaAroundUp(CurrentRotation, DesiredRotation, GravityUp);
		}
	}

	// Apply any turning from the animation sequence
	const float LastCurveValue = TurnData.CurveValue;
	const FTurnInPlaceCurveValues CurveValues = GetCurveValues();
	const float TurnYawWeight = CurveValues.TurnYawWeight;

	if (FMath::IsNearlyZero(TurnYawWeight, KINDA_SMALL_NUMBER))
	{
		// No curve weight, don't apply any animation yaw
		TurnData.CurveValue = 0.f;
		TurnData.bLastUpdateValidCurveValue = false;
	}
	else
	{
		// Apply the remaining yaw from the current animation (curve) that is playing, scaled by the weight curve
		const float RemainingTurnYaw = CurveValues.RemainingTurnYaw;
		const float NewCurveValue = RemainingTurnYaw * TurnYawWeight;

		const bool bWasValid = TurnData.bLastUpdateValidCurveValue;
		TurnData.bLastUpdateValidCurveValue = true;

		// Only apply a delta if we already had a valid curve value last frame; the first relevant frame just
		// captures the starting point. (Previously this branch zeroed TurnData.CurveValue, which then caused
		// FMath::Sign(0) != FMath::Sign(non-zero) to skip the *second* frame's delta as well, losing curve
		// progress and contributing to oscillation.)
		if (bWasValid)
		{
			// Don't apply if a direction change occurred (this avoids snapping when changing directions).
			// A previous value of 0 (e.g. resuming from an idle/recovery frame) is treated as same-direction.
			const bool bSameDirection = LastCurveValue == 0.f
				|| NewCurveValue == 0.f
				|| FMath::Sign(NewCurveValue) == FMath::Sign(LastCurveValue);
			if (bSameDirection)
			{
				// Exceeding 180 degrees results in a snap, so maintain current rotation until the turn animation
				// removes the excessive angle
				const float NewTurnOffset = TurnData.TurnOffset + (NewCurveValue - LastCurveValue);
				if (FMath::Abs(NewTurnOffset) <= 180.f)
				{
					TurnData.TurnOffset = NewTurnOffset;
				}
			}
		}

		TurnData.CurveValue = NewCurveValue;
	}

	// Clamp the turn in place to the max angle if provided; this prevents the character from under-rotating in
	// relation to the control rotation which can cause the character to insufficiently face the camera in shooters
	const FGameplayTag& TurnModeTag = GetTurnModeTag();
	const FTurnInPlaceAngles* TurnAngles = Params.GetTurnAngles(TurnModeTag);
	if (!TurnAngles)
	{
		UE_LOG(LogTurnInPlace, Warning, TEXT("No TurnAngles found for TurnModeTag: %s"), *TurnModeTag.ToString());
	}

	// Clamp the turn offset to the max angle if provided
	const float MaxTurnAngle = TurnAngles ? TurnAngles->MaxTurnAngle : 0.f;
	if (MaxTurnAngle > 0.f && FMath::Abs(TurnData.TurnOffset) > MaxTurnAngle)
	{
		TurnData.TurnOffset = FMath::ClampAngle(TurnData.TurnOffset, -MaxTurnAngle, MaxTurnAngle);
	}

	if (!bClientSimulation)
	{
		// How much yaw the actor should rotate this frame, measured as a heading difference about the gravity up axis.
		// (FullOffset = yaw delta from actor to control; Remaining = TurnData.TurnOffset after curve deduction;
		//  the actor closes the gap by FullOffset - Remaining each tick.)
		const float FullOffset = TurnInPlaceLocal::ComputeYawDeltaAroundUp(CurrentRotation, DesiredRotation, GravityUp);
		const float ActorTurnRotation = FRotator::NormalizeAxis(FullOffset - TurnData.TurnOffset);

		// Rotate about the gravity up axis and re-vertical the capsule to gravity, so the body stays gravity-aligned
		// (including while standing still, where ActorTurnRotation is ~0).
		ApplyGravityVertical(CurrentRotation, ActorTurnRotation);
	}
	
#if !UE_BUILD_SHIPPING
	// Log the turn in place values for debugging if set to verbose
	const FString NetRole = GetNetMode() == NM_Standalone ? TEXT("") : GetOwner()->GetLocalRole() == ROLE_Authority ? TEXT("[ Server ]") : TEXT("[ Client ]");
	UE_LOG(LogTurnInPlace, Verbose, TEXT("%s cv %.2f  lcv %.2f  offset %.2f"), *NetRole, TurnData.CurveValue, LastCurveValue, TurnData.TurnOffset);
#endif
}

void UTurnInPlace::PostTurnInPlace(float LastTurnOffset)
{
	// Compress result and replicate to simulated proxy
	CompressSimulatedTurnOffset(LastTurnOffset);
}

bool UTurnInPlace::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::FaceRotation);

	// We only want to handle rotation if we are using FaceRotation() and not PhysicsRotation() based on our movement settings
	if (GetTurnMethod() != ETurnMethod::FaceRotation)
	{
		return true;
	}

	// Invalid requirements, exit
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCharacterMovement())
	{
		TurnData = {};
		return true;
	}

	// When DeltaTime is 0 this call is not the player controller asking us to face the control rotation; it is
	// UCharacterMovementComponent::UpdateBasedMovement asking the actor to follow the movement base's rotation
	// (see Engine/Private/Components/CharacterMovementComponent.cpp where FaceRotation is invoked with 0.f).
	// Running the TurnInPlace logic on that input would treat the per-tick base rotation delta as a fresh turn
	// request, overwriting TurnData.TurnOffset with the tiny base delta and stealing the player's actual turn
	// intent. The engine's UpdateBasedRotation then applies the actor's delta back to ControlRotation, producing
	// the feedback loop where a turn on a rotating base rotates the camera and re-triggers another turn.
	// Defer to APawn::FaceRotation (and the engine's MoveUpdatedComponent fallback) for that pass-through.
	if (DeltaTime == 0.f)
	{
		return false;
	}

	// Determine the correct params to use
	const FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);
	
	// Turn in place is locked, we can't do anything
	const bool bEnabled = State != ETurnInPlaceEnabledState::Paused;
	if (!bEnabled)
	{
		TurnData = {};
		return false;
	}

	// Cache the current rotation
	const FRotator CurrentRotation = GetOwner()->GetActorRotation();

	// If the character is stationary, we can turn in place
	if (IsCharacterStationary())
	{
		TurnInPlace(CurrentRotation, NewControlRotation);
		return true;
	}

	// Moving - clear turn-in-place state. We keep InterpOutAlpha (it drives the slerp below) but reset the curve
	// fields so a subsequent stationary turn starts from a clean first-relevant-frame guard rather than from
	// stale CurveValue / bLastUpdateValidCurveValue carried in from a previous, interrupted turn.
	TurnData.TurnOffset = 0.f;
	TurnData.CurveValue = 0.f;
	TurnData.bLastUpdateValidCurveValue = false;

	// This is ACharacter::FaceRotation(), but with interpolation for when we start moving so it doesn't snap
	if (!MaybeCharacter->GetCharacterMovement()->bOrientRotationToMovement)
	{
		if (MaybeCharacter->bUseControllerRotationPitch || MaybeCharacter->bUseControllerRotationYaw || MaybeCharacter->bUseControllerRotationRoll)
		{
			if (!MaybeCharacter->bUseControllerRotationPitch)
			{
				NewControlRotation.Pitch = CurrentRotation.Pitch;
			}

			if (!MaybeCharacter->bUseControllerRotationYaw)
			{
				NewControlRotation.Yaw = CurrentRotation.Yaw;
			}
			else
			{
				// Interpolate away the rotation because we are moving
				TurnData.InterpOutAlpha = FMath::FInterpConstantTo(TurnData.InterpOutAlpha, 1.f, DeltaTime, Params.MovingInterpOutRate);
				NewControlRotation.Yaw = FQuat::Slerp(CurrentRotation.Quaternion(), NewControlRotation.Quaternion(), TurnData.InterpOutAlpha).GetNormalized().Rotator().Yaw;
			}

			if (!MaybeCharacter->bUseControllerRotationRoll)
			{
				NewControlRotation.Roll = CurrentRotation.Roll;
			}

#if ENABLE_NAN_DIAGNOSTIC
			if (NewControlRotation.ContainsNaN())
			{
				logOrEnsureNanError(TEXT("APawn::FaceRotation about to apply NaN-containing rotation to actor! New:(%s), Current:(%s)"), *NewControlRotation.ToString(), *CurrentRotation.ToString());
			}
#endif

			GetOwner()->SetActorRotation(NewControlRotation);
		}
	}
	return true;
}

bool UTurnInPlace::PhysicsRotation(UCharacterMovementComponent* CharacterMovement, float DeltaTime,
	bool bRotateToLastInputVector, const FVector& LastInputVector)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::PhysicsRotation);

	// We only want to handle rotation if we are using PhysicsRotation() and not FaceRotation() based on our movement settings
	if (GetTurnMethod() != ETurnMethod::PhysicsRotation)
	{
		return false;
	}
	
	// Invalid requirements, exit
	if (!HasValidData() || !MaybeCharacter || !MaybeCharacter->GetCharacterMovement())
	{
		TurnData = {};
		return true;
	}

	// Determine the correct params to use
	const FTurnInPlaceParams Params = GetParams();
	
	// Determine the state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);
	
	// Turn in place is locked, we can't do anything
	const bool bEnabled = State != ETurnInPlaceEnabledState::Paused;
	if (!bEnabled)
	{
		TurnData = {};
		return false;
	}

	// Cache the updated component and current rotation
	const USceneComponent* UpdatedComponent = CharacterMovement->UpdatedComponent;
	const FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("UTurnInPlace::PhysicsRotation(): CurrentRotation"));

	// If the character is stationary, we can turn in place
	if (IsCharacterStationary())
	{
		if (bRotateToLastInputVector && CharacterMovement->bOrientRotationToMovement)
		{
			// Rotate towards the last input vector
			TurnInPlace(CurrentRotation, LastInputVector.Rotation());
		}
		else if (CharacterMovement->bUseControllerDesiredRotation && MaybeCharacter->Controller)
		{
			// Rotate towards the controller's desired rotation
			TurnInPlace(CurrentRotation, MaybeCharacter->Controller->GetDesiredRotation());
		}
		else if (!MaybeCharacter->Controller && CharacterMovement->bRunPhysicsWithNoController && CharacterMovement->bUseControllerDesiredRotation)
		{
			// We have no controller, but we can try to find one
			if (const AController* ControllerOwner = Cast<AController>(MaybeCharacter->GetOwner()))
			{
				// Rotate towards the controller's desired rotation
				TurnInPlace(CurrentRotation, ControllerOwner->GetDesiredRotation());
			}
		}
		return true;
	}

	// We've started moving, CMC can take over by calling Super::PhysicsRotation()
	TurnData = {};  // Cull turn offset when we start moving, it will be recalculated when we stop moving
	return false;
}

FTurnInPlaceAnimGraphData UTurnInPlace::UpdateAnimGraphData(float DeltaTime) const
{
	FTurnInPlaceAnimGraphData AnimGraphData;
	if (!HasValidData())
	{
		return AnimGraphData;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::UpdateAnimGraphData);

	// Get the current turn in place anim set & parameters from the animation blueprint
	AnimGraphData.AnimSet = GetTurnInPlaceAnimSet();
	const FTurnInPlaceParams Params = AnimGraphData.AnimSet.Params;

	// Determine the enabled state of turn in place
	const ETurnInPlaceEnabledState State = GetEnabledState(Params);

	// Retrieve parameters for the current frame required by the animation graph
	const float& TurnOffset = GetTurnOffset();
	AnimGraphData.TurnOffset = TurnOffset;
	AnimGraphData.bIsTurning = IsTurningInPlace();
	AnimGraphData.StepSize = DetermineStepSize(Params, TurnOffset, AnimGraphData.bTurnRight);
	AnimGraphData.TurnModeTag = GetTurnModeTag();
	AnimGraphData.bWantsPseudoAnimState = WantsPseudoAnimState();

	// Abort the turn if we became unable to turn in place during a turn
	AnimGraphData.bAbortTurn = State != ETurnInPlaceEnabledState::Enabled && CanAbortTurnAnimation();

	// Determine if we have valid turn angles for the current turn mode tag and cache the result
	if (const FTurnInPlaceAngles* TurnAngles = Params.GetTurnAngles(GetTurnModeTag()))
	{
		AnimGraphData.TurnAngles = *TurnAngles;
		AnimGraphData.bHasValidTurnAngles = true;
		AnimGraphData.bWantsToTurn = State != ETurnInPlaceEnabledState::Locked && Params.StepSizes.Num() > 0 &&
			FMath::Abs(TurnOffset) >= TurnAngles->MinTurnAngle;
	}
	else
	{
		AnimGraphData.bHasValidTurnAngles = false;
		UE_LOG(LogTurnInPlace, Warning, TEXT("No TurnAngles found for TurnModeTag: %s"), *AnimGraphData.TurnModeTag.ToString());
	}

	return AnimGraphData;
}

void UTurnInPlace::PostUpdateAnimGraphData(float DeltaTime, FTurnInPlaceAnimGraphData& AnimGraphData, FTurnInPlaceAnimGraphOutput& TurnOutput)
{
	// Note: We only have valid TurnOutput here if we are updating the pseudo anim state (i.e. dedicated server only!)
	UpdatePseudoAnimState(DeltaTime, AnimGraphData, TurnOutput);
}

void UTurnInPlace::ThreadSafeRefreshAnimGraphData(FTurnInPlaceAnimGraphData& AnimGraphData,
	const FTurnInPlaceCurveValues& CurveValues, bool& bWasTurningThisEntry) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::ThreadSafeRefreshAnimGraphData);

	if (!HasValidData())
	{
		AnimGraphData.bWasTurningThisEntry = bWasTurningThisEntry;
		return;
	}

	// Refresh bIsTurning against the just-cached curve so the state machine doesn't see a 2-frame stale value
	AnimGraphData.bIsTurning = !FMath::IsNearlyZero(CurveValues.TurnYawWeight, KINDA_SMALL_NUMBER);

	// Latch: once TurnYawWeight has fired this entry, it stays latched until the anim graph resets it on the next
	// TurnInPlace entry. This is what blocks the recovery transition on the entry frame when curves are still stale.
	if (AnimGraphData.bIsTurning)
	{
		bWasTurningThisEntry = true;
	}
	AnimGraphData.bWasTurningThisEntry = bWasTurningThisEntry;

	// Note: we intentionally do NOT recompute bAbortTurn / bWantsToTurn here. Both depend on CanAbortTurnAnimation()
	// (a BlueprintNativeEvent) and on game-thread-only overrides (camera mode, root motion montage, etc.) captured
	// in NativeUpdateAnimation. The Pause/Lock-curve threshold fix in OverrideTurnInPlace_Implementation handles
	// the curve overshoot case that was the main contributor to frame-stale flicker; remaining 1-2 frame lag for
	// game-thread-driven aborts is harmless once the latch eliminates the entry-frame recovery firing.
}

void UTurnInPlace::UpdatePseudoAnimState(float DeltaTime, const FTurnInPlaceAnimGraphData& TurnAnimData,
	FTurnInPlaceAnimGraphOutput& TurnOutput)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::UpdatePseudoAnimState_Entry);

	// Dedicated server might want to use pseudo anim state instead of playing actual animations
	if (!WantsPseudoAnimState())
	{
		return;
	}

	if (!HasValidData())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::UpdatePseudoAnimState);

	// Update pseudo state on dedicated server
	const FTurnInPlaceAnimSet& AnimSet = TurnAnimData.AnimSet;

	switch (PseudoAnimState)
	{
	case ETurnPseudoAnimState::Idle:
		if (TurnOutput.bWantsToTurn)
		{
			PseudoAnimState = ETurnPseudoAnimState::TurnInPlace;

			// SetupTurnAnim()
			PseudoNodeData.StepSize = TurnAnimData.StepSize;
			PseudoNodeData.bIsTurningRight = TurnAnimData.bTurnRight;

			// SetupTurnInPlace()
			PseudoNodeData.AnimStateTime = 0.f;
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation_AnimSet(AnimSet, PseudoNodeData, false);
			PseudoNodeData.bHasReachedMaxTurnAngle = false;
			UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceNode(PseudoNodeData, TurnAnimData, AnimSet);
		}
		break;
	case ETurnPseudoAnimState::TurnInPlace:
		if (TurnOutput.bAbortTurn)
		{
			PseudoAnimState = ETurnPseudoAnimState::Idle;

			// SetupIdle()
			PseudoNodeData.TurnPlayRate = 1.f;
			PseudoNodeData.bHasReachedMaxTurnAngle = false;
		}
		else if (TurnOutput.bWantsTurnRecovery)
		{
			PseudoAnimState = ETurnPseudoAnimState::Recovery;

			// SetupTurnRecovery() -- AnimStateTime is already carried over from TurnInPlace
			PseudoNodeData.bIsRecoveryTurningRight = PseudoNodeData.bIsTurningRight;
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation_AnimSet(AnimSet, PseudoNodeData, true);
		}
		else
		{
			// UpdateTurnInPlace()
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation_AnimSet(AnimSet, PseudoNodeData, false);
			PseudoNodeData.AnimStateTime = UTurnInPlaceStatics::GetUpdatedTurnInPlaceAnimTime_ThreadSafe(PseudoAnim,
				PseudoNodeData.AnimStateTime, DeltaTime, PseudoNodeData.TurnPlayRate);
			UTurnInPlaceStatics::ThreadSafeUpdateTurnInPlaceNode(PseudoNodeData, TurnAnimData, AnimSet);
		}
		break;
	case ETurnPseudoAnimState::Recovery:
		{
			// UpdateTurnInPlaceRecovery()
			PseudoAnim = UTurnInPlaceStatics::GetTurnInPlaceAnimation_AnimSet(AnimSet, PseudoNodeData, true);
			PseudoNodeData.AnimStateTime = UTurnInPlaceStatics::GetUpdatedTurnInPlaceAnimTime_ThreadSafe(PseudoAnim,
				PseudoNodeData.AnimStateTime, DeltaTime, 1.f);  // Recovery plays at 1x speed
			if (!PseudoAnim || (PseudoAnim && PseudoNodeData.AnimStateTime >= PseudoAnim->GetPlayLength()))
			{
				PseudoAnimState = ETurnPseudoAnimState::Idle;

				// SetupIdle()
				PseudoNodeData.TurnPlayRate = 1.f;
				PseudoNodeData.bHasReachedMaxTurnAngle = false;
			}
		}
		break;
	}
}

int32 UTurnInPlace::DetermineStepSize(const FTurnInPlaceParams& Params, float Angle, bool& bTurnRight)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UTurnInPlace::DetermineStepSize);
	
	// Cache the turn angle and step angle
	const float TurnAngle = Angle;
	const float StepAngle = FMath::Abs(TurnAngle) + Params.SelectOffset;

	// Determine if we are turning right or left
	bTurnRight = TurnAngle > 0.f;

	// No step sizes, return 0
	if (Params.StepSizes.Num() == 0)
	{
		ensureMsgf(false, TEXT("No StepSizes found in TurnInPlaceParams"));
		return 0;
	}

	// Determine the step size based on the select mode
	int32 StepSize = 0;
	switch(Params.SelectMode)
	{
	case ETurnAnimSelectMode::Nearest:
		{
			// Find the animation nearest to the angle
			float Diff = 0.f;
			for (int32 i = 0; i < Params.StepSizes.Num(); i++)
			{
				const int32& TAngle = Params.StepSizes[i];
				const float AngleDiff = FMath::Abs(StepAngle - (float)TAngle);
				if (i == 0 || AngleDiff < Diff)
				{
					Diff = AngleDiff;
					StepSize = i;
				}
			}
		}
		break;
	case ETurnAnimSelectMode::Greater:
		{
			// Find the highest animation that exceeds the angle
			for (int32 i = 0; i < Params.StepSizes.Num(); i++)
			{
				const int32& TAngle = Params.StepSizes[i];
				if (FMath::FloorToInt(StepAngle) >= TAngle)
				{
					StepSize = i;
				}
			}
		}
		break;
	default: ;
	}

	return StepSize;
}

void UTurnInPlace::DebugRotation() const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (!IsValid(GetOwner()))
	{
		return;
	}

	// Optionally draw server's physics bodies so we can visualize what they're doing animation-wise
	DebugServerPhysicsBodies();

	// Turn Offset Screen Text
	if (TurnInPlaceCvars::bDebugTurnOffset && GEngine)
	{
		// Don't overwrite other character's screen messages
		const uint64 DebugKey = GetOwner()->GetUniqueID() + 1569;
		const FRandomStream ColorStream(DebugKey);
		const FColor DebugColor = FColor(ColorStream.RandRange(0, 255), ColorStream.RandRange(0, 255), ColorStream.RandRange(0, 255));
		const FString CharacterRole = GetOwner()->HasAuthority() ? TEXT("Server") : GetOwner()->GetLocalRole() == ROLE_AutonomousProxy ? TEXT("Client") : TEXT("Simulated");
		GEngine->AddOnScreenDebugMessage(DebugKey, 0.5f, DebugColor, FString::Printf(TEXT("[ %s ] TurnOffset: %.2f"), *CharacterRole, GetTurnOffset()));
	}

	// We only want each character on screen to draw this once, so exclude servers from drawing this for the autonomous proxy
	if (GetOwner()->GetRemoteRole() != ROLE_AutonomousProxy)
	{
		// Draw Debug Arrows
		bool bValidLocation = false;
		const FVector Location = GetDebugDrawArrowLocation(bValidLocation);
		if (!bValidLocation)
		{
			return;
		}

		// Actor Rotation Vector
		if (TurnInPlaceCvars::bDebugActorDirectionArrow)
		{
			DrawDebugDirectionalArrow(GetOwner()->GetWorld(), Location,
				Location + (GetOwner()->GetActorForwardVector() * 200.f), 40.f, FColor(199, 10, 143),
				false, -1, 0, 2.f);
		}

		// Control Rotation Vector
		if (TurnInPlaceCvars::bDebugControlDirectionArrow && GetController())
		{
			DrawDebugDirectionalArrow(GetOwner()->GetWorld(), Location,
				Location + (FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f).Vector() * 200.f), 40.f,
				FColor::Black, false, -1, 0, 2.f);
		}

		// Turn Rotation Vector
		if (TurnInPlaceCvars::bDebugTurnOffsetArrow)
		{
			const FVector TurnVector = (GetOwner()->GetActorRotation() + FRotator(0.f, GetTurnOffset(), 0.f)).GetNormalized().Vector();
			DrawDebugDirectionalArrow(GetOwner()->GetWorld(), Location, Location + (TurnVector * 200.f),
				40.f, FColor(38, 199, 0), false, -1, 0, 2.f);
		}
	}
#endif
}

#if UE_ENABLE_DEBUG_DRAWING
#if !WITH_SIMPLE_ANIMATION
static bool bHasWarnedSimpleAnimation = false;
#endif
#endif
void UTurnInPlace::DebugServerPhysicsBodies() const
{
#if UE_ENABLE_DEBUG_DRAWING
	// Draw Server's physics bodies
	if (bDrawServerPhysicsBodies && PawnOwner && GetOwner()->GetLocalRole() == ROLE_Authority && GetNetMode() != NM_Standalone)
	{
#if WITH_SIMPLE_ANIMATION
		USimpleAnimLib::DrawPawnDebugPhysicsBodies(PawnOwner, GetMesh(), true, false, false);
#else
		if (!bHasWarnedSimpleAnimation)
		{
			bHasWarnedSimpleAnimation = true;
			const FText ErrorMsg = FText::Format(
				LOCTEXT("NoSimpleAnimPlugin", "{0} is trying to draw server animation but SimpleAnimation plugin was not found. Disable UTurnInPlace::bDrawServerAnimation"),
				FText::FromString(GetName()));
#if WITH_EDITOR
			// Show a notification in the editor
			FNotificationInfo Info(ErrorMsg);
			Info.ExpireDuration = 6.f;
			FSlateNotificationManager::Get().AddNotification(Info);

			// Log the error to message log
			FMessageLog("PIE").Error(ErrorMsg);
#else
			// Log the error to the output log
			UE_LOG(LogTurnInPlace, Error, TEXT("%s"), *ErrorMsg.ToString());
#endif
		}
#endif
	}
#endif
}

#undef LOCTEXT_NAMESPACE