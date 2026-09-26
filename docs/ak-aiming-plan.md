# AE AK spine aiming: next implementation

Use the AK and Quantum character under AE Master's `Assets/AE` only. The `KINEMATION/CharacterAnimationSystem` scripts are a behavior reference; do not import CAS Demo assets. The reference `FPSCopyBonesJob` copies weapon and both hand targets before later procedural modifiers and applies weapon offsets in the source rotation frame. TheEngine's [AE_AK.rig](../tests/assets/AE_AK.rig) establishes the same dependency with engine-owned controls.

## Pose order

```text
AK locomotion/idle/reload clip + upper-body layers
  -> OnAnimatorPose jobs
  -> precopy: ik_hand_gun -> head/vb_ak_weapon
  -> prerotate: correct vb_ak_weapon in its copied source frame
  -> precopy: ik_hand_l/r -> vb_ak_weapon/vb_ak_hand_l/r
  -> bounded additive pitch/yaw over spine_01..05, neck_01, head
  -> optional aim/sway weapon modifiers, then Two Bone IK left and right arms
  -> AK scene object socketed to vb_ak_weapon
  -> skinning
```

The helpers under `head` are non-skinned children. First copy the authored weapon transform, then apply its local rotation correction, then copy the hand targets in **model space**. The AK rig combines a -90-degree local X correction with a 180-degree local Z roll so the copied weapon points toward character forward (-Z) with its top side up (+Y). The former +90-degree correction pointed it backward, while -90 degrees alone left it upside down. Tune the authoritative value in the `.rig` file. This preserves the authored neutral hand positions despite the weapon correction. The later spine/head bend carries the weapon helper and both grip helpers together; arm IK then reattaches the hands to the carried grips. This keeps authored reload/inspect target motion instead of pinning hands in one static world location. Unreal documents virtual bones as alternate-parent targets for exactly the rifle/head-look swimming problem; its common root-parented example is a useful comparison, while head parenting is the deliberate choice here because the weapon must follow the aimed head frame. [Unreal Virtual Bones](https://dev.epicgames.com/documentation/unreal-engine/virtual-bones-in-unreal-engine)

## Aim driver and controls

1. Compute pitch and yaw from the camera direction in character-local space, after character yaw is known. Clamp to per-weapon limits; use body turn for yaw beyond the comfortable spine range. Filter the angles for visual smoothness, but do not filter the IK grip positions separately.
2. Author the neutral AK pose first. Drive a configurable additive look profile, initially `spine_01` through `head`, with per-bone pitch/yaw shares that total one. Keep pelvis and legs outside this pass. Compare four corners and center at several movement speeds; tune shares to avoid neck over-rotation. An aim-offset pose grid can replace or augment procedural shares when art-direction is needed. [Unreal Aim Offset](https://dev.epicgames.com/documentation/unreal-engine/aim-offset-in-unreal-engine)
3. Expose Copy Bone channel flags and control weight, Modify Bone reference space and offset, and Two Bone IK tip/effector/pole/weight in a structured Rig editor. Keep all model/local conversions inside the animation stream. Unreal's skeletal controls and Unity's stream handles both make these spaces explicit. [Unreal Skeletal Controls](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-skeletal-controls-in-unreal-engine), [Unity TransformStreamHandle](https://docs.unity.com/en-us/engine/6000.7/script-reference/unityengine/animations/transformstreamhandle)
4. Use `vb_ak_weapon` as the weapon socket. Treat the right hand as the primary grip and the left as the support grip. Give each arm a pole target and clamp reach; provide separate IK weights during reload, regrip and inspect so an authored hand can release the weapon. Two Bone IK's effector and joint target are the corresponding Unreal controls. [Unreal Two Bone IK](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-two-bone-ik-in-unreal-engine)

## Validation before shipping

Run the real AE idle, walk, fire, tactical reload and empty reload clips with the Quantum body and AK model. Check center, up/down, left/right and diagonal aim; measure both hand-to-grip errors and weapon-to-camera alignment each frame. Scrub transitions, blend IK weights during reload, test animation root motion, and confirm no one-frame target jumps when entering/exiting aim. Draw weapon, grip and elbow-pole axes in the Scene view. The current self-test covers AE idle import, a runtime rig pass and neutral/angled screenshots. Those screenshots expose an unfinished weapon-model alignment: the separately imported weapon FBX has its own animated parts, including a detached magazine in the static pose. Align the weapon mesh to `vb_ak_weapon` and evaluate its weapon animation in sync with the character before calling the visual setup complete.

The graph/stream design follows Unity's source/mixer/job model: jobs receive a writable animation stream before final output, and handles resolve bones against the active skeleton. [Unity PlayableGraph](https://docs.unity.com/en-us/engine/6000.6/manual/animation-section/animation-mecanim/playables/graph), [Unity AnimationScriptPlayable](https://docs.unity.com/en-us/engine/6000.7/script-reference/unityengine/animations/animationscriptplayable), [Unity AnimationStream](https://docs.unity.com/en-us/engine/6000.7/script-reference/unityengine/animations/animationstream)

For repeatable remote review, `tools/Test-AkAnimation.ps1` captures neutral and angled PNGs, combines them in one labeled image, and writes per-pose weapon transforms and hand-to-grip distances to a CSV beside the full self-test log.
