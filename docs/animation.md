# Animation and rigging

## Data flow

The asset browser imports FBX models and clips through `src/anim/FbxImport.cpp` and `src/render/Resources.cpp`. A model contains mesh parts, a skeleton and, when present, animation stacks. `ClipLibrary` in `Animation.cpp` loads clips and caches bone-name bindings. `AnimatorController.cpp` evaluates states, transitions and blend trees. `AnimationSystem.cpp` applies the resulting pose, root motion and optional rig modifiers, then builds joint matrices for GPU skinning.

Dropping a rigged model into a scene creates mesh entities and enables an Animator on the root. The bones stay in the imported skeleton; they are not ordinary scene entities. A clip can drive another skeleton when bone names match. Clip rotations transfer by name, while translation handling is limited to root/hip motion and authored IK or marker tracks. This is not a general humanoid retargeter.

## Animator Controllers

TheEngine's `.controller` files are text assets. Version 2 stores a list of layers; version 1 single-layer files still load as a Base Layer. Each layer owns states, transitions and a default state. States play a clip or a 1D/2D blend tree. Parameters are float, int, bool or trigger. Transitions may use conditions, exit time and a duration; an Any State transition can enter a state from anywhere.

The Base Layer supplies the full-body pose and root motion. Upper layers can use override or additive blending and a bone-subtree mask. Override layers may use mesh-space rotation on the top bones of a mask. The 2D tree is freeform Cartesian; directional blend trees and nested trees are not implemented. Layer weight can be changed from C# with `Animator.SetLayerWeight`, and the Animator window exposes layers, masks, state graphs and transition editing.

TheEngine controller assets are not Unity Animator Controller YAML. Unity `.controller` and `.mask` files need an explicit conversion or re-authoring step.

## Root motion and modifiers

When Apply Root Motion is enabled, clip root travel moves the scene entity and is removed from the skinned pose. Scripts with `OnAnimatorMove` can take ownership of that movement; `Animator.deltaPosition` and `deltaRotation` expose the last update. A look-bone list distributes `Animator.SetLookAngles` over selected spine/neck/head bones. Hand IK can solve arm chains toward `ik_hand_r` and `ik_hand_l` bones if the required bones exist in the rig. Dynamic bone chains add spring motion after animation.

The optional `.rig` setup extends an imported skeleton with helper bones and ordered copy/move/rotate operations. A Bone Socket component places a child scene object on a named bone and can follow either position only or position and rotation. Socket offsets are in bone space. These facilities are general engine features; the clean AnimationSetup scene does not depend on a `.rig` asset, hand IK or sockets.

## Where to change behavior

- Import or retargeting: `FbxImport.cpp`, `Animation.cpp` and `Animation.h`.
- State evaluation, masks and layer blending: `AnimatorController.cpp` and `AnimatorController.h`.
- Root motion, pose modifiers, rig helpers and sockets: `AnimationSystem.cpp` and `AnimationSystem.h`.
- Scene component persistence: `Scene.cpp` and `Scene.h`.
- Editing and script API: `EditorAnimator.cpp`, `EditorPanels.cpp`, `scripting/ScriptCore/Animator.cs`.

Run the engine self-test after controller or import changes, then open a real project and confirm the animated mesh and transitions in play mode. The self-test covers state transitions, 1D/2D blend trees and root motion; it does not prove visual parity with a Unity animation package.
