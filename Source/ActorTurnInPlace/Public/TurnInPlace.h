// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TurnInPlaceTypes.h"
#include "Components/ActorComponent.h"
#include "TurnInPlace.generated.h"

#define TURN_ROTATOR_TOLERANCE	(1.e-3f) 

class UCharacterMovementComponent;
struct FGameplayTag;
/**
 * Core TurnInPlace functionality
 * This is added to your ACharacter subclass which must override ACharacter::FaceRotation() to call ULMTurnInPlace::FaceRotation()
 */
UCLASS(Blueprintable, HideCategories=(Variable, Sockets, Tags, ComponentTick, ComponentReplication, Activation, Cooking, Events, AssetUserData, Replication, Collision))
class ACTORTURNINPLACE_API UTurnInPlace : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Turn)
	FTurnInPlaceSettings Settings;

	UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly, Category=Turn)
	TObjectPtr<ACharacter> Character;
	
	UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly, Category=Turn)
	TObjectPtr<UAnimInstance> AnimInstance;

	/** Cached checks when AnimInstance changes */
	UPROPERTY()
	bool bIsValidAnimInstance;

	UPROPERTY(EditDefaultsOnly, Category=Turn)
	bool bWarnIfAnimInterfaceNotImplemented;
	
	UPROPERTY(Transient)
	bool bHasWarned;
	
public:
	UTurnInPlace(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnRegister() override;
	virtual void PostLoad() override;
	virtual void InitializeComponent() override;

	virtual void SetUpdatedCharacter();
	virtual void BeginPlay() override;
	virtual void DestroyComponent(bool bPromoteChildren = false) override;

protected:
	UFUNCTION()
	virtual void OnAnimInstanceChanged();
	
public:
	/**
	 * Character is currently turning in place if the TurnYawWeight curve is not 0
	 * @return True if the character is currently turning in place
	 */
	UFUNCTION(BlueprintPure, Category=Turn)
	bool IsTurningInPlace() const;

	UFUNCTION(BlueprintPure, Category=Turn)
	bool IsCharacterMoving() const { return !IsCharacterStationary(); }

	UFUNCTION(BlueprintPure, Category=Turn)
	bool IsCharacterStationary() const;

	/**
	 * Get the current root motion montage that is playing
	 * @return The current root motion montage
	 */
	UFUNCTION(BlueprintCallable, Category=Turn)
	UAnimMontage* GetCurrentNetworkRootMotionMontage() const;

	/**
	 * Optionally override determine when to ignore root motion montages
	 * @param Montage The montage to check
	 * @return True if the montage should be ignored
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	bool ShouldIgnoreRootMotionMontage(const UAnimMontage* Montage) const;

	/**
	 * Get the character's mesh component that is used for turn in place
	 * @return The character's mesh
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	USkeletalMeshComponent* GetMesh() const;
	
	/**
	 * Optionally override the Turn In Place parameters to force turn in place to be enabled or disabled
	 * When Turn In Place is disabled, the character's rotation is locked in current direction
	 * 
	 * Default: Use the params from the animation blueprint to determine if turn in place should be enabled or disabled
	 * ForceEnabled: Always enabled regardless of the params from the animation blueprint
	 * ForceLocked: Always locked in place and will not rotate regardless of the params from the animation blueprint
	 * ForceDisabled: Will not accumulate any turn offset, allowing normal behaviour expected of a system without any turn in place. Useful for root motion montages.
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	ETurnInPlaceOverride OverrideTurnInPlace() const;
	virtual ETurnInPlaceOverride OverrideTurnInPlace_Implementation() const;

	/**
	 * Used for determining if the character is currently in a pivot anim state
	 * Only required if you blend turn rotation using EInterpOutMode::AnimationCurve instead of EInterpOutMode::Interpolation 
	 * @see UTurnInPlace::PhysicsRotation()
	 * @see EInterpOutMode
	 * @return True if the character is currently in a pivot anim state
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	bool IsPivoting() const;
	virtual bool IsPivoting_Implementation() const { return false; }

	/**
	 * Used to tell the PhysicsRotation() to reinitialize
	 * @return True if the character wants to reinitialize the physics rotation
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	bool WantsToReinitializePhysicsRotation() const;
	virtual bool WantsToReinitializePhysicsRotation_Implementation() const { return false; }
	
	/**
	 * TurnMode is used to determine which FTurnInPlaceAngles to use
	 * This allows having different min and max turn angles for different modes
	 * @return GameplayTag corresponding to the current turn mode
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn, meta=(GameplayTagFilter="TurnMode."))
	FGameplayTag GetTurnModeTag() const;

public:
	/**
	 * The current turn offset in degrees
	 * @note Epic refer to this as RootYawOffset but that's not accurate for an actor-based turning system
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	float TurnOffset;

	/**
	 * The current value of the curve represented by TurnYawCurveName
	 */
	UPROPERTY(BlueprintReadOnly, Category=Turn)
	float CurveValue;

	/** When the character starts moving, interpolate away the turn in place */
	UPROPERTY(BlueprintReadOnly, Category=Turn)
	float InterpOutAlpha;

private:
	UPROPERTY(Transient)
	bool bLastUpdateValidCurveValue;

public:
	/** Supplied by CMC for use in the anim instance */
	UPROPERTY(Transient, BlueprintReadOnly, Category=Turn)
	float InitialStartAngle;

	bool bStartRotationInitialized = false;
	bool bWasInPivotState = false;
	
	FRotator StartRotation = FRotator::ZeroRotator;
	FVector PrevStartAcceleration = FVector::ZeroVector;
	FRotator TargetRotation = FRotator::ZeroRotator;
	float StartAngle = 0.f;
	float StartRotationRate = 0.f;

public:
	ETurnInPlaceEnabledState GetEnabledState(const FTurnInPlaceParams& Params) const;
	FTurnInPlaceParams GetParams() const;
	FTurnInPlaceCurveValues GetCurveValues() const;

	virtual bool HasValidData() const;

	virtual ETurnMethod GetTurnMethod() const;
	
	virtual void TurnInPlace(const FRotator& CurrentRotation, const FRotator& DesiredRotation);
	
	/**
	 * Must be called from your ACharacter::FaceRotation() override
	 * Do not call Super::FaceRotation()
	 * This updates the turn in place rotation
	 *
	 * Character Mesh must have an animation blueprint applied for this to function, it will exit without it
	 *
	 * @param NewControlRotation The NewControlRotation from ACharacter::FaceRotation()
	 * @param DeltaTime DeltaTime from ACharacter::FaceRotation()
	 */
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime);

	/**
	 * Must be called from UCharacterMovementComponent::PhysicsRotation override
	 * Handles Start + Pivot animations that blend their own rotation with CMC to override it
	 * @return True if PhysicsRotation() was handled and CMC should not continue
	 */
	virtual bool PhysicsRotation(UCharacterMovementComponent* CharacterMovement, float DeltaTime,
		bool bRotateToLastInputVector = false, const FVector& LastInputVector = FVector::ZeroVector);
	
	/** Call when a root motion montage is played so we can deinitialize */
	void OnRootMotionIsPlaying();

	UFUNCTION(BlueprintCallable, Category=Turn)
	FTurnInPlaceAnimGraphData UpdateAnimGraphData() const;

protected:
	/** Used to determine which step size to use based on the current TurnOffset and the last FTurnInPlaceParams */
	static int32 DetermineStepSize(const FTurnInPlaceParams& Params, float Angle, bool& bTurnRight);

#if ENABLE_ANIM_DEBUG
protected:
	void DebugRotation() const;
#endif
};
