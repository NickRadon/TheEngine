# Animation and rigging

## Data flow

The asset browser imports FBX models and clips through `src/anim/FbxImport.cpp` and `src/render/Resources.cpp`. A model contains mesh parts, a skeleton and, when present, animation stacks. `ClipLibrary` in `Animation.cpp` loads clips and caches bone-name bindings. `AnimatorController.cpp` evaluates states, transitions and blend trees. `AnimationSystem.cpp` applies the resulting pose, root motion and optional rig modifiers, then builds joint matrices for GPU skinning.

Dropping a rigged model into a scene creates mesh entities and enables an Animator on the root. The bones stay in the imported skeleton; they are not ordinary scene entities. A clip can drive another skeleton when bone names match. Clip rotations transfer by name, while translation handling is limited to root/hip motion and authored IK or marker tracks. This is not a general humanoid retargeter.

## Animator Controllers

TheEngine's `.controller` files are plain text. The first line is `TheEngineAnimator <version>`:

| Version | Written by | Content |
| --- | --- | --- |
| 1 | first release | one layer: params, states, transitions, default state |
| 2 | layers update | adds `layer` lines (name, weight, blending, mask bones, `@meshspace`) |
| 3 | interruption update | adds `interrupt <mode> ordered <0\|1>` to every `transition` line |
| 4 | blend mask assets | adds an optional `@mask "path"` reference to a `layer` line |

Loaders accept versions 1 through 4: version 1 and 2 files gain `interruption = none`, `ordered = 0`, and an absent `active` field in a scene defaults to true, so existing projects and controllers keep working unchanged. Only new saves are written as version 4.

Line types in order: `param`, `layer`, `state` (+ indented `child` lines for blend trees), `transition` (+ indented `condition` lines), `default`, `entry`, `any`.

Parameters are float, int, bool or trigger. States play a clip or a 1D/2D freeform blend tree. Transitions may use conditions, exit time and a duration in seconds; an Any State transition can enter a state from anywhere. Exit times are compared against the state a transition leaves, and a non-looping state only crosses its exit time when time actually grows past it, so a paused or zero-length state cannot fire spurious transitions.

### Interruption

Each transition has an **interruption source** (the `interrupt` tail of the transition line):

| Mode | Meaning |
| --- | --- |
| `none` | the running transition always finishes (the pre-3.0 behavior) |
| `current` | transitions leaving the state the blend starts from |
| `next` | transitions leaving the destination state |
| `current_next` / `next_current` | both groups, checked in the listed order |

`ordered 1` (Unity's "Ordered Interruption") checks candidates in the order they appear in the layer instead of trying Any State transitions first.

When a candidate fires mid-blend, the interrupted blend is **not** snapshotted and not discarded: its states are frozen at their current weights and stored as up to four weighted motion sources that keep playing (their times advance, they keep producing root motion) as the "from" side of the new transition. There is no pose pop and root motion keeps flowing. Sources beyond four are merged by dropping the lightest one and renormalizing.

### Damping

`AnimatorInstance::SetParamDamped(name, target, dampTime, dt)` steps a float/int parameter exponentially towards the target with `1 - exp(-dt / dampTime)`, once per call; `dampTime <= 0` or `dt <= 0` set the value directly. The engine stores no separate target, so scripts should call it once per frame with the frame's `deltaTime` (C#: `Animator.SetFloat(name, value, dampTime, deltaTime)`).

### Layers and the additive reference pose

The Base Layer supplies the full-body pose and root motion. Upper layers can use override or additive blending and a bone-subtree mask (a bone is in the mask when it or one of its ancestors is listed). Override layers may use mesh-space rotation on the top bones of a mask. **Additive layers add the difference between the layer's pose and its reference pose, where the reference pose is the layer's current state sampled at normalized time 0** (the state's first frame). Layer weight scales both blend modes; the base layer always has weight 1.

The 2D tree is freeform Cartesian; directional blend trees and nested trees are not implemented. Layer weight can be changed from C# with `Animator.SetLayerWeight`, and the Animator window exposes layers, masks, state graphs and transition editing.

### Blend mask assets

A reusable blend mask is a plain-text `.mask` asset. Its first line is `TheEngineMask 1`, followed by one `bone "name"` line for each included bone. The list is ancestor-closed at runtime: including `spine_01` drives that bone and all of its descendants, which is the usual upper-body mask. Use a whole-body Base Layer underneath it for lower-body locomotion; including `pelvis` would also include the spine, so a single include-only mask cannot express "legs except spine."

Create one from **Project > Create > Blend Mask**, double-click it, select an animated object whose skeleton supplies the bone tree, and tick the roots the layer should drive. Assign the asset from an upper layer's **Mask Asset** field in the Animator window. Assigning an asset replaces that layer's legacy inline comma-separated mask; old controllers can continue using the inline list.

An empty inline mask means whole body for compatibility. Once a mask asset is assigned, an empty asset drives nothing. A missing or unreadable asset also drives nothing and produces a validation warning, so deleting an upper-body mask cannot silently turn that layer into a full-body override. Blend Mask window changes save immediately to the separate asset and are not included in the controller's Undo/Redo history.

TheEngine controller assets are not Unity Animator Controller YAML. Unity `.controller` and `.mask` files need an explicit conversion or re-authoring step.

## Validation and debug information

`AnimatorController::Validate(ClipLibrary*, const Skeleton*)` returns a list of `AnimIssue`s: missing or unloadable clips, blend trees without a parameter/motions, duplicate thresholds, transitions with no exit time and no conditions, conditions that name unknown parameters, transitions to missing states, missing/duplicate default states, states unreachable from the default state, and mask bones that are not in the rig. `clips` and `skeleton` may be null to skip the checks that need them.

The runtime logs the issues once, when a controller is loaded or changes on disk outside the editor. The Animator window recomputes them every frame, shows a **Warnings (n)** button in the status bar (click an issue to select the offending state or transition), marks those nodes and arrows with an amber badge, and repeats the message in the selection panel. The Inspector shows the warning count for the assigned controller.

The status bar of the Animator window also shows the live state of the selected layer while playing: current and destination state, transition progress, whether the running transition was interrupted, normalized time and layer weight, plus Undo/Redo buttons.

## Editing and undo

The Animator window edits the controller asset directly and saves it when a change settles (no mouse button down and no widget active). Controller edits have their own undo history, separate from scene undo: the text of the controller from before the current edit stroke is pushed once per stroke (a drag, a click, an Enter commit), so dragging a node creates one entry. Ctrl+Z / Ctrl+Y, and the Edit menu items, go to the controller history while the Animator window has focus and to the scene history otherwise; the same shortcuts show "Undo Controller" in that case.

## Root motion and modifiers

When Apply Root Motion is enabled, clip root travel moves the scene entity and is removed from the skinned pose. Root motion of a non-looping state stops at the end of the clip. Scripts with `OnAnimatorMove` can take ownership of that movement; `Animator.deltaPosition` and `Animator.deltaRotation` expose the last update. The Animator header checkbox in the Inspector (`AnimatorComponent::active`, `Animator.active` in C#) turns the component off at runtime without removing it: a disabled Animator does not update and keeps its current pose. A look-bone list distributes `Animator.SetLookAngles` over selected spine/neck/head bones. Hand IK can solve arm chains toward `ik_hand_r` and `ik_hand_l` bones if the required bones exist in the rig. Dynamic bone chains add spring motion after animation.

The optional `.rig` setup extends an imported skeleton with non-skinned helper bones and ordered controls. Create it from **Project > Create > Rig Controls**, edit it in the dedicated Rig Controls window, and assign it in the Animator Inspector. Version 1 files remain loadable. Version 2 supports `precopy` (before the spine look pass), `copy` (after it), `modify` (a weighted position and rotation offset in a reference bone's frame), and `twobone` (tip, effector bone, optional pole bone, weight). Copy Bone has independent translation, rotation and scale flags. Entries run top to bottom; helper `bone` lines must precede controls that reference them. For example:

```text
TheEngineRig 2
bone "weapon" "head" 0 0 0 0 0 0 1
precopy "weapon" "ik_hand_gun" 1 1 1 1
twobone "hand_l" "ik_hand_l" "" 1
```

The runtime evaluates controller -> writable stream jobs -> pre-look copies -> spine/head look -> remaining ordered rig controls (including IK) -> optional legacy hand IK -> dynamic bones -> sockets and skinning. Turn off **Legacy Hand IK** when the `.rig` contains explicit hand solvers. A Bone Socket can attach the weapon object to the head-parented virtual weapon helper. The `.rig` editor saves separately from scene undo.

`AnimationGraph` is the C++ playable surface: controller or clip sources feed weighted mixers and writable stream jobs. Mixer weight and clip speed/time can be changed between evaluations. `AnimationStream` binds named bones to handles, reads and writes local or model transforms, and exposes root motion. A graph can be built in code and evaluated against any compatible skeleton; the Animator uses a controller source and a stream job for its standard pipeline. C# scripts can declare `void OnAnimatorPose(AnimationStream stream)` and call `TryGetLocal`, `TryGetModel`, `SetLocal`, or `SetModel`. The stream works only during the callback, before rig controls. Its `BonePose` contains position, rotation and scale.

The sample [AE AK rig](../tests/assets/AE_AK.rig) uses helper bones under `head`, copies the authored AK weapon and IK-hand targets before the look pass, then solves both arms. Copy this fixture into a project's `Assets` folder to assign it in the Inspector. The self-test reads AK animations and the Quantum body from AE Master's `Assets/AE` folder; it does not modify that Unity project or copy its assets.

## Scripting API (`scripting/ScriptCore/Animator.cs`)

- Parameters: `SetFloat/GetFloat`, `SetBool/GetBool`, `SetInteger/GetInteger`, `SetTrigger/ResetTrigger`, damped `SetFloat(name, value, dampTime, deltaTime)`, and enumeration through `parameterCount` / `TryGetParameter` (name, type, live value).
- States: `GetState(layer)` returns a `LayerState` (state indices, normalized time, transition progress, in-transition flag, interrupted flag, layer weight) in one call; `currentStateName`, `StateName(layer, destination)`, `IsInState`, `normalizedTime`, `IsInTransition`, `transitionProgress`, `IsTransitionInterrupted`.
- Layers: `layerCount`, `GetLayerName`, `GetLayerIndex`, `GetLayerWeight`, `SetLayerWeight`.
- Root motion: `deltaPosition`, `deltaRotation`, `applyRootMotion`, `SetLookAngles`.
- Pose job: `OnAnimatorPose(AnimationStream stream)` with local/model bone reads and writes (valid only inside the callback).
- Component: `enabled` (component present), `active` (runtime enable).

Native entry points live in `ScriptNativeApi` (`src/scripting/ScriptEngine.cpp`) and must keep the same field order as `TheEngine.Internal.NativeApi` (`scripting/ScriptCore/Native.cs`); append new functions at the end of both lists.

The Animator instance is created and evaluated by the first animation update after play mode starts, so a script's `Start()` still sees the "nothing is playing" defaults (`layerCount` 0, empty state and layer names, `parameterCount` 0, zeroed `LayerState`). Query states from `Update()` onwards.

## Where to change behavior

- Import or retargeting: `FbxImport.cpp`, `Animation.cpp` and `Animation.h`.
- State evaluation, transitions, interruption, masks and layer blending: `AnimatorController.cpp` and `AnimatorController.h`.
- Root motion, pose modifiers, rig helpers and sockets: `AnimationSystem.cpp` and `AnimationSystem.h`.
- Scene component persistence: `Scene.cpp` and `Scene.h`.
- Editing, validation display and script API: `EditorAnimator.cpp`, `EditorPanels.cpp`, `scripting/ScriptCore/Animator.cs`.

Run the engine self-test after controller or import changes (`TheEngine.exe --selftest`), then open a real project and confirm the animated mesh and transitions in play mode. The self-test covers controller serialization for versions 1/2/3/4, blend mask serialization and asset-backed upper/lower-body masking, state transitions, interruption (with and without the feature), 1D/2D blend trees, damping, layers with masks/weights/additive blending, exit times, root motion, validation, the Animator window's controller undo/redo and disk write-back, Blend Mask window write-back, and the C# `Animator` API (component properties, the runtime-enable flag, and the documented defaults when no Animator instance runs); it does not prove visual parity with a Unity animation package.
