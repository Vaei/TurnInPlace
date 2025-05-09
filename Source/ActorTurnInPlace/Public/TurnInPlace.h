// Copyright (c) 2025 Jared Taylor

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

	/**
	 * Allows server to optionally update without playing actual animations
	 * Pseudo is helpful if we don't want to refresh bones on tick for the mesh for performance reasons
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	ETurnAnimUpdateMode DedicatedServerAnimUpdateMode = ETurnAnimUpdateMode::Animation;

	/**
	 * Allow simulated proxies to parse their animation curves to deduct turn offset
	 * This prevents them being stuck in a turn while awaiting their next replication update if the server ticks at a
	 * low frequency which is common in released products but unlikely to be seen in your new project with default settings
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Turn)
	bool bSimulateAnimationCurves = true;
	
	/** Turn in place settings */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Turn)
	FTurnInPlaceSettings Settings;

	/** Owning character that we are turning in place */
	UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly, Category=Turn)
	TObjectPtr<APawn> PawnOwner;
	
	/** Owning character that we are turning in place */
	UPROPERTY(Transient, DuplicateTransient, BlueprintReadOnly, Category=Turn)
	TObjectPtr<ACharacter> MaybeCharacter;

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
	virtual void InitializeComponent() override;

	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	void CacheUpdatedCharacter();
	
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
	 * Determine if we are under the control of a root motion montage
	 * Generally this is a call to ACharacter::IsPlayingNetworkedRootMotionMontage()
	 * You must override this if not using ACharacter
	 * @return True if under the control of a root motion montage
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	bool IsPlayingNetworkedRootMotionMontage() const;
	
	/**
	 * Optionally override determine when to ignore root motion montages
	 * @param Montage The montage to check
	 * @return True if the montage should be ignored
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	bool ShouldIgnoreRootMotionMontage(const UAnimMontage* Montage) const;

	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	ETurnInPlaceOverride GetOverrideForMontage(const UAnimMontage* Montage) const;
	
	/**
	 * This function is primarily used for debugging, if the controller doesn't exist debugging won't work
	 * @return The controller if one exists
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	AController* GetController() const;
	
	/**
	 * Get the character's mesh component that is used for turn in place
	 * @return The character's mesh
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	USkeletalMeshComponent* GetMesh() const;

	/**
	 * Generally this is where the character's feet are
	 * @param bIsValidLocation
	 * @return The location to draw debug arrows from
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	FVector GetDebugDrawArrowLocation(bool& bIsValidLocation) const;
	
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
	 * If true we can abort a turn animation if we become unable to turn in place during the animation
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn)
	bool CanAbortTurnAnimation() const;
	virtual bool CanAbortTurnAnimation_Implementation() const { return true; }
	
	/**
	 * TurnMode is used to determine which FTurnInPlaceAngles to use
	 * This allows having different min and max turn angles for different modes
	 * @return GameplayTag corresponding to the current turn mode
	 */
	UFUNCTION(BlueprintNativeEvent, Category=Turn, meta=(GameplayTagFilter="TurnMode."))
	FGameplayTag GetTurnModeTag() const;

public:
	/**
	 * Get the current turn offset in degrees
	 * @return The current turn offset in degrees
	 * @note Epic refer to this as RootYawOffset but that's not accurate for an actor-based turning system, especially because this value is the inverse of actual root yaw offset
	 * @warning You generally do not want to factor this into your anim graph when considering velocity, acceleration, or aim offsets because we have a true rotation and it is unnecessary
	 */
	UFUNCTION(BlueprintPure, Category=Turn)
	const float& GetTurnOffset() const { return TurnData.TurnOffset; }
	
public:
	/** Transient data that is updated each frame */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	FTurnInPlaceData TurnData;

	/**
	 * Current pseudo anim state on dedicated server
	 * Must never be modified on game thread
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	ETurnPseudoAnimState PseudoAnimState;

	/** Data typically used by the anim graph, borrowed for pseudo anim nodes */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	FTurnInPlaceGraphNodeData PseudoNodeData;

	/** Current pseudo anim sequence to fake on dedicated server, queried for curve values */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category=Turn)
	TObjectPtr<UAnimSequence> PseudoAnim;

public:
	/** Get the current turn in place state that determines if turn in place is enabled, paused, or locked */
	ETurnInPlaceEnabledState GetEnabledState(const FTurnInPlaceParams& Params) const;

	/** Retrieve the turn in place anim set */
	FTurnInPlaceAnimSet GetTurnInPlaceAnimSet() const;
	
	/** Get the current turn in place parameters */
	FTurnInPlaceParams GetParams() const;

	/** Get the current turn in place curve values that were cached by the animation graph */
	FTurnInPlaceCurveValues GetCurveValues() const;

	/** Dedicated server updates the turn in place curve values manually */
	virtual bool WantsPseudoAnimState() const;
	
	/** @return True if the TurnInPlace component has valid data */
	virtual bool HasValidData() const;

	/** Which method to use for turning in place. Either PhysicsRotation() or FaceRotation() */
	virtual ETurnMethod GetTurnMethod() const;

	static bool HasTurnOffsetChanged(float CurrentValue, float LastValue);

	/**
	 * Must be called from your ACharacter::Tick() override
	 * Allows simulated proxies to simulate the deduction based on the anim curve
	 * This is helpful for servers that have low tick frequency so that the sim proxy doesn't get stuck in a turn state
	 * while awaiting the next replication update
	 */
	virtual void SimulateTurnInPlace();

	/** Process the core logic of the TurnInPlace system */
	virtual void TurnInPlace(const FRotator& CurrentRotation, const FRotator& DesiredRotation, bool bClientSimulation = false);

	/** Must be called from your ACharacter::FaceRotation() and UCharacterMovementComponent::PhysicsRotation() overrides */
	virtual void PostTurnInPlace(float LastTurnOffset);
	
	/**
	 * Must be called from your ACharacter::FaceRotation() override
	 * This updates the turn in place rotation
	 *
	 * @param NewControlRotation The NewControlRotation from ACharacter::FaceRotation()
	 * @param DeltaTime DeltaTime from ACharacter::FaceRotation()
	 * @return True if FaceRotation() was handled and ACharacter should not call Super::FaceRotation()
	 */
	virtual bool FaceRotation(FRotator NewControlRotation, float DeltaTime);

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
	FTurnInPlaceAnimGraphData UpdateAnimGraphData(float DeltaTime) const;

	/** Called immediately after UpdateAnimGraphData() for post-processing */
	UFUNCTION(BlueprintCallable, Category=Turn)
	void PostUpdateAnimGraphData(float DeltaTime, FTurnInPlaceAnimGraphData& AnimGraphData, FTurnInPlaceAnimGraphOutput& TurnOutput);
	
	/**
	 * Called from Anim Graph BlueprintThreadSafeUpdateAnimation or NativeThreadSafeUpdateAnimation
	 * Thread safe only, do not update anything that has a basis on the game thread
	 */
	virtual void UpdatePseudoAnimState(float DeltaTime, const FTurnInPlaceAnimGraphData& TurnAnimData,
		FTurnInPlaceAnimGraphOutput& TurnOutput);

protected:
	/** Used to determine which step size to use based on the current TurnOffset and the last FTurnInPlaceParams */
	static int32 DetermineStepSize(const FTurnInPlaceParams& Params, float Angle, bool& bTurnRight);

public:
	/** Debug the turn in place properties if enabled */
	virtual void DebugRotation() const;

protected:
	/** Debug server's anims by drawing physics bodies. Must be called externally from character's Tick() */
	virtual void DebugServerPhysicsBodies() const;
};
