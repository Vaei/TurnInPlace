# Actor Turn In Place <img align="right" width=128, height=128 src="https://github.com/Vaei/TurnInPlace/blob/main/Resources/Icon128.png">

> [!IMPORTANT]
> Actor-Based Turn in Place (TIP) Solution. A superior substitute to Mesh-Based Turn in Place without the endless list of issues that comes with it.

> [!WARNING]
> Use `git clone` instead of downloading as a zip, or you will not receive content
> <br>Install this as a project plugin, not an engine plugin

> [!TIP]
> Supports UE5.3+

> [!NOTE]
> [Read the Wiki for Instructions and Complete Features!](https://github.com/Vaei/TurnInPlace/wiki/How-to-Use)

## Watch Me

> [!TIP]
[Showcase Video](https://t.co/tUQ1csqX6k)

## How to Use
> [!IMPORTANT]
> [Read the Wiki for Instructions and Complete Features!](https://github.com/Vaei/TurnInPlace/wiki/How-to-Use)

# Technique Comparison

## Actor-Based TIP

Actor-Based TIP does not have any of the issues that come with Mesh-Based TIP. Read below for a detailed list.

We don't rotate the mesh, we simply use the existing functionality from `ACharacter::FaceRotation()` and `UCharacterMovementComponent::PhysicsRotation()` to store a `TurnOffset` on the `UTurnInPlace` Actor Component then apply it when we apply rotation to the Character. That's it!

This is my own solution that I developed from scratch because no adequate solution existed. This is released for free because it should become the industry standard for Unreal Engine Turn In Place.

### Feature Rich

_Multiplayer Ready: No extra steps required_

Provides turn in place for the following movement types:
* `ACharacter::bUseControllerRotationYaw` for strafing movement used in shooters (Lyra uses this exclusively)
* `UCharacterMovementComponent::bUseControllerDesiredRotation` for strafing movement that interpolates smoothly
* `UCharacterMovementComponent::bOrientRotationToMovement` by turning towards the `LastInputVector`, which has been added to the Character Movement Component for you

Built with different stances in mind, allowing for different step sizes (e.g. 60, 90, 135) for different stances. But none of this is limited to stances, you can determine which AnimSet to use based on anything you like.

Handles increasing the turn rate when you reach the max turn angle, and also when you change directions mid-turn, e.g. playing left turn but now turning to the right, the left turn can complete rapidly using a specific multiplier.

Handles montages out of the box. Plenty of functions to override to determine behaviour in the `UTurnInPlace` component.

### Clean & Contained

This system is very condensed. There is a `TurnInPlace` UActorComponent that is responsible for the functionality, and functions that you can call to from your Anim Graph that handle everything you need.

## Mesh-Based TIP

Primarily this setup is seen in LyraStarterGame. It is inadequate and causes significant issues everywhere it touches.

This technique negates the rotation from the character's mesh by applying a `RootYawOffset` to the skeleton's root bone, using the `Rotate Root Bone` anim graph node.

### Mesh Smoothing

Simulated proxies are characters other than your own that you see in multiplayer games. Simulated proxies receive a very simplistic condensed location and rotation from the server, then applies smoothing directly to the mesh so that you don't notice the considerable jitter due to intermittent replication and compression of these properties.

This mesh smoothing applies to the root bone rotation, and they fight each other. This causes considerable jitter that increases with latency. The jitter will be particularly noticable when the `RootYawOffset` is higher, and a turn has not been initiated.

To counteract this, you must switch `NetworkSmoothingMode` from the default `Exponential` to `Linear`. Exponential looks really good, its distance based, linear doesn't look particularly good. You also need to increase the `NetworkSimulatedSmoothRotationTime` to help mask this defect, but now your simulated proxies are very late updating to their actual facing direction, which has quality issues but also gameplay issues for fast action-based competitive games.

The larger issue with using `Linear` over `Exponential` isn't rotation, its translation. Linear translation looks truly poor. Your sim proxies constantly get _yoinked_ backwards when they come to a stop and the start doesn't look great either!

This leaves you making serious sacrifices for something that is entirely _not important_ affecting every aspect of your game. One option is to modify the engine to decouple the `NetworkSmoothingMode` so that you can set Translation separately from Rotation, however that is very difficult and can be prone to issues that are extremely difficult to diagnose because they are tightly interconnected.

### Locomotion / Anim Compensation

We now have _additional rotation_ to factor into every single system we build. Do we want to use a rotation based on the actual mesh rotation, or do we want the mesh rotation based on where we _see_ it facing based on the `RootYawOffset`?

Furthermore, when adding procedural systems, however simple, they might fight your `Rotate Root Bone`, especially when sockets come into play which you will often find to be a frame behind the `Rotate Root Bone`!

### Anim Graph Overload

There is too much in the anim graph that goes into building the system Lyra uses.

# Changelog

### _Future Updates_
* Custom Gravity
* Mover 2.0 out of the box support
	* This depends on Mover 2.0 ever becoming actually _good_ and it doesn't look promising

### 1.3.4
* Fixed critical bug where ShouldIgnoreRootMotionMontage() condition was incorrectly inverted - **This is potentially breaking**
* Added ability to set override on per-montage basis (check your anim set params)
* Const-correctness

### 1.3.3
* Replace native pointers with TObjectPtr

### 1.3.2
* Change Editor module to EditorNoCommandlet
	* Project-specific use-cases can interfere with commandlets

### 1.3.1
* Add missing include (generally inconsequential)

### 1.3.0
**BREAKING CHANGES**
_Consider using perforce to diff `ABP_Manny_Turn` in particular when updating your project_

* Fixed move combining by resetting transient data at the start of the move instead of reapplying the last turn yaw to `StartRotation`
* Introduced pseudo animation system for dedicated servers that don't refresh bones (and don't run any turn anim graph states at all)
	* Updated `ABP_Manny_Turn` to support updated system that has been condensed with pseudo anim states in mind
	* Condensed multiple anim graph variables into `FTurnInPlaceGraphNodeData AnimNodeData`
	* Condensed anim graph functionality into single C++ nodes where appropriate
		* `GetTurnInPlaceAnimation()`
		* `ThreadSafeUpdateTurnInPlaceNode()`
* Introduced the ability for simulated proxies to parse their anim curves to deduct turn offset
	* This prevents them being stuck in a turn while awaiting their next replication update if the server ticks at a low frequency (common in released products but not new/default UE projects)
	* Enabled by default -- results remain stable regardless of tick frequency
* Add missing `SetupTurnInPlaceRecovery()` node function from `ABP_Manny_Turn`
	* This is potentially inconsequential but added for sake of completion and consistency
* Add cheat CVar `p.Turn.Override` for debugging/isolation testing
* Add `TRACE_CPUPROFILER_EVENT_SCOPE` for profiling via insights
* Make virtual `DebugRotation` and `DebugServerPhysicsBodies` for projects to remove/add debugging functionality

### 1.2.0
* Backport demo content for 5.3
	* Tidied it up a bit too
* Fix included BP_GameMode, was referencing unavailable class
* Remove `CacheUpdatedCharacter()` from `UTurnInPlace::PostLoad()`, fails `check` calling `BlueprintNativeEvent`

### 1.1.0
* Add support for `APawn` without `ACharacter` + `UCharacterMovementComponent`
	* Extremely advanced -- [Read the Wiki entry](https://github.com/Vaei/TurnInPlace/wiki/APawn-Support)

### 1.0.4
* Improved UPROPERTY descriptors for FTurnInPlaceParams and FTurnInPlaceAnimSet considerably

### 1.0.3
* Fix bug where TurnOffset not reset when using strafe direct and moving

### 1.0.2
* Fixed bug where `ETurnInPlaceEnabledState` wasn't always handled properly
* Added demo map buttons to preview `ETurnInPlaceEnabledState`

### 1.0.1
* Create component directly and unhide category to allow assignment to BP component

### 1.0.0
* Initial Release
