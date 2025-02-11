// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "TurnInPlaceTypes.h"
#include "Components/ActorComponent.h"
#include "TurnInPlace.generated.h"

#define TURN_ROTATOR_TOLERANCE	(1.e-3f)

class ACharacter;
class UCharacterMovementComponent;
class UAnimInstance;
struct FGameplayTag;
/**
 * Core TurnInPlace functionality
 * This is added to your ACharacter subclass which must override ACharacter::FaceRotation() to call ULMTurnInPlace::FaceRotation()
 */
UCLASS(Blueprintable, BlueprintType, meta=(BlueprintSpawnableComponent), HideCategories=(Sockets, Tags, ComponentTick, Activation, Cooking, Events, AssetUserData, Replication, Collision, Navigation))
class ACTORTURNINPLACE_API UTurnInPlace : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Draw server's physics bodies in editor - non-shipping builds only, not available in standalone
	 * Allows us to visualize what the server is doing animation-wise
	 * 
	 * Requires SimpleAnimation plugin to be present and enabled
	 * https://github.com/Vaei/SimpleAnimation
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	bool bDrawServerPhysicsBodies = false;
	
	/** Turn in place settings */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Turn)
	FTurnInPlaceSettings Settings;

	/** Owning character that we are turning in place */
	UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly, Category=Turn)
	TObjectPtr<ACharacter> Character;

	/** AnimInstance of the owning character's Mesh */
	UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly, Category=Turn)
	TObjectPtr<UAnimInstance> AnimInstance;

	/** Cached checks when AnimInstance changes */
	UPROPERTY()
	bool bIsValidAnimInstance;

	/** If true, will warn if the owning character's AnimInstance does not implement ITurnInPlaceAnimInterface */
	UPROPERTY(EditDefaultsOnly, Category=Turn)
	bool bWarnIfAnimInterfaceNotImplemented;

protected:
	/** Prevents spamming of the warning */
	UPROPERTY(Transient)
	bool bHasWarned;

	/**
	 * Server replicates to simulated proxies by compressing TurnInPlace::TurnOffset from float to uint16 (short)
	 * Simulated proxies decompress the value to float and apply it to the TurnInPlace component
	 * This keeps simulated proxies in sync with the server and allows them to turn in place
	 */
	UPROPERTY(ReplicatedUsing=OnRep_SimulatedTurnOffset)
	FTurnInPlaceSimulatedReplication SimulatedTurnOffset;

public:
	UTurnInPlace(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	ENetRole GetLocalRole() const;
	bool HasAuthority() const;

	void CompressSimulatedTurnOffset(float LastTurnOffset);

	UFUNCTION()
	void OnRep_SimulatedTurnOffset();

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

	/** @return True if the character is currently moving */
	UFUNCTION(BlueprintPure, Category=Turn)
	bool IsCharacterMoving() const { return !IsCharacterStationary(); }

	/** @return True if the character is currently stationary (not moving) */
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
	 * TurnMode is used to determine which FTurnInPlaceAngles to use
	 * This allows having different min and max turn angles for different modes
	 * @return GameplayTag corresponding to the current turn mode
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn, meta=(GameplayTagFilter="TurnMode."))
	FGameplayTag GetTurnModeTag() const;

public:
	/**
	 * The current turn offset in degrees
	 * @note Epic refer to this as RootYawOffset but that's not accurate for an actor-based turning system, especially because this value is the inverse of actual root yaw offset
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	float TurnOffset;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	float LastAppliedTurnYaw;

	/**
	 * The current value of the curve represented by TurnYawCurveName
	 */
	UPROPERTY(BlueprintReadOnly, Category=Turn)
	float CurveValue;

	/** When the character starts moving, interpolate away the turn in place */
	UPROPERTY(BlueprintReadOnly, Category=Turn)
	float InterpOutAlpha;

private:
	/** Whether the last update had a valid curve value -- used to check if becoming relevant again this frame */
	UPROPERTY(Transient)
	bool bLastUpdateValidCurveValue;

public:
	/** Get the current turn in place state that determines if turn in place is enabled, paused, or locked */
	ETurnInPlaceEnabledState GetEnabledState(const FTurnInPlaceParams& Params) const;

	/** Get the current turn in place parameters */
	FTurnInPlaceParams GetParams() const;

	/** Get the current turn in place curve values that were cached by the animation graph */
	FTurnInPlaceCurveValues GetCurveValues() const;

	/** @return True if the TurnInPlace component has valid data */
	virtual bool HasValidData() const;

	/** Which method to use for turning in place. Either PhysicsRotation() or FaceRotation() */
	virtual ETurnMethod GetTurnMethod() const;

	static bool HasTurnOffsetChanged(float CurrentValue, float LastValue);

	/** Process the core logic of the TurnInPlace system */
	virtual void TurnInPlace(const FRotator& CurrentRotation, const FRotator& DesiredRotation);

	/** Must be called from your ACharacter::FaceRotation() and UCharacterMovementComponent::PhysicsRotation() overrides */
	virtual void PostTurnInPlace(float LastTurnOffset);
	
	/**
	 * Must be called from your ACharacter::FaceRotation() override
	 * This updates the turn in place rotation
	 *
	 * @param NewControlRotation The NewControlRotation from ACharacter::FaceRotation()
	 * @param DeltaTime DeltaTime from ACharacter::FaceRotation()
	 */
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime);

	/**
	 * Must be called from UCharacterMovementComponent::PhysicsRotation() override
	 * @return True if PhysicsRotation() was handled and CMC should not call Super::PhysicsRotation()
	 */
	virtual bool PhysicsRotation(UCharacterMovementComponent* CharacterMovement, float DeltaTime,
		bool bRotateToLastInputVector = false, const FVector& LastInputVector = FVector::ZeroVector);

	/**
	 * Used by the anim graph to request the data pertinent to the current frame and trigger the turn in place animations
	 */
	UFUNCTION(BlueprintCallable, Category=Turn)
	FTurnInPlaceAnimGraphData UpdateAnimGraphData() const;

protected:
	/** Used to determine which step size to use based on the current TurnOffset and the last FTurnInPlaceParams */
	static int32 DetermineStepSize(const FTurnInPlaceParams& Params, float Angle, bool& bTurnRight);

public:
	/** Debug the turn in place properties if enabled */
	void DebugRotation() const;

protected:
	/** Debug server's anims by drawing physics bodies. Must be called externally from character's Tick() */
	void DebugServerPhysicsBodies() const;
};
