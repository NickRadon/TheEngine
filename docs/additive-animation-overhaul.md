# Additive animation overhaul

## Research and fact check (2026-10-01)

The goal is reliable, authorable animation layering: recoil, breathing, pose corrections and aiming must preserve the underlying action and remain continuous during transitions. This changes the engine, not the separate AnimationSetup project.

Primary sources checked:

1. [Unity animation layers](https://docs.unity3d.com/Manual/AnimationLayers.html): masks and additive/override layers are established authoring tools. Unity cautions that animated properties must match. This does not establish a market-share ranking.
2. [Unity reference frame API](https://docs.unity3d.com/ScriptReference/ModelImporterClipAnimation-additiveReferencePoseFrame.html): reference frames are explicit authoring data, not necessarily frame zero.
3. [Unreal Aim Offset](https://dev.epicgames.com/documentation/unreal-engine/aim-offset-in-unreal-engine): mesh-space additive rotations keep aiming direction consistent when the underlying body leans. Samples use a common neutral animation frame. Mesh space is appropriate for aiming, not universally better than local space.
4. [ozz additive sample](https://guillaumeblanc.github.io/ozz-animation/samples/additive/): reference-relative animation, layer weights and joint masks; additive combination follows ordinary blending.
5. [ozz delta builder source](https://github.com/guillaumeblanc/ozz-animation/blob/master/src/animation/offline/additive_animation_builder.cc) and [runtime blending source](https://github.com/guillaumeblanc/ozz-animation/blob/master/src/animation/runtime/blending_job.cc): translation differences, quaternion composition and multiplicative scale ratios. Quaternion multiplication order must match the extraction/application convention; copying one half of another engine's equation is incorrect.

Common approaches compared:

| Approach | Appropriate use | Tradeoff / decision |
| --- | --- | --- |
| Local reference-relative deltas | Recoil, breathing, facial/finger motion | Default; inexpensive; follows parent axes |
| Mesh-space rotation deltas | Aim offsets over leaning/turning poses | Opt-in; requires hierarchy passes; translations remain local |
| Offline delta baking | Large immutable clip libraries | Efficient but adds an asset pipeline and cache invalidation; defer until profiling justifies it |
| Runtime reference extraction | Existing imported clips and live authoring | Chosen here; compatible with retargeted samples and editor changes |
| Procedural offsets plus final IK | Weapon correction and hand contact | Preserve existing post-animation rig stage; additive blending alone cannot guarantee grip |
| Override layers | Reload/inspect actions replacing a body region | Retain; these actions should not automatically become additive |

## Audit findings

- The old implementation blends absolute source/destination poses, then subtracts only the current state's first frame. Different neutral poses therefore introduce transition offsets and completion pops.
- Interrupted blends have the same reference mismatch; blend-tree clips also need individual neutral extraction.
- Explicit neutral clips exist in version 5 but are locked to time zero and silently fall back if missing.
- Scale deltas are dropped. Additive mesh-space rotation is ignored.
- Existing exact/subtree masks and base-only root motion should remain intact.
- Working tree contains unrelated Unity templates and review images. Preserve source changes and back up review images before refreshing them.

## Implementation plan and acceptance gates

1. **Reference contract and persistence.** Add normalized reference time and optional skeleton-rest reference. Preserve empty-clip first-frame defaults and version 1–5 loading; write version 6. Expose controls in the Animator editor. Warn on invalid reference settings; a missing explicit reference contributes identity, never an unrelated fallback.
2. **Delta-first evaluation.** Extract each contributing clip against its own selected reference before blend-tree, transition and interrupted-source blending. Apply authored state offsets before extraction. Use identity translation/rotation/scale for empty additive motions. Keep root extraction symmetric between source and reference.
3. **Complete composition.** Translation differences, normalized shortest-path quaternion deltas, guarded scale ratios. Keep existing local rotation convention (delta times base). Implement component-space rotation deltas with quaternion hierarchy passes, reconstructing locals against the already-updated parent; do not derive rotations from scaled matrices. Preserve mask behavior and layer order.
4. **Authoring and guidance.** Document reference choices, space, neutral poses, stack order (base/actions, additive details, rig/IK), migration behavior and limits. No forced conversion of reload/inspect animations and no edits to AnimationSetup.
5. **Regression proof.** Test differing-reference transitions and interruption, blend trees, neutral/zero/half/full weights, antipodal rotations, scale ratios and zero reference scale, mesh-space hierarchy/masks, reference time/rest/invalid clip, serialization and root-motion isolation. Run existing animation suite and fix failures caused by this work.
6. **Visual and remote review.** Use the repo MCP bridge if available, running Test-AkAnimation.ps1 through it. Inspect both generated views and report grip metrics. These capture existing AK rig integration, not every additive feature; synthetic tests prove the math. Commit only this work plus refreshed latest PNGs, push, and share immutable HTTPS GitHub image links.

## Fact-check conclusions before implementation

- Confirmed by source: additive poses require a reference; local and mesh-space rotations serve different purposes; scale requires ratio composition.
- Engineering deduction to verify in tests: converting each source to deltas before mixing removes reference-switch artifacts, including interruption and blend-tree changes.
- Explicit compatibility decision: preserve this engine's left-multiplied local deltas; do not silently adopt ozz's rotation convention.
- No claim that a particular approach is statistically most popular, that mesh space fixes IK, or that a still image proves temporal smoothness.
- Performance scope: linear hierarchy passes; avoid matrix decomposition. Runtime reference sampling remains a known cost, with offline baking deferred.

## Execution record

Plan recorded before implementation. Results and justified deviations will be appended here.

All six implementation stages completed. The runtime, editor controls, version-6 persistence and user guide follow the recorded reference and composition contract. Local layers avoid allocating mesh-space hierarchy arrays.

Validation: **91 passed, 0 failed** with `tools/Test-AkAnimation.ps1 -SkipBuild -EnginePath build/additive-bin/TheEngine.exe`. Added 21 assertions covering the planned mathematical/compatibility cases. Existing rig, controller, masking and animation tests also pass. The first run passed the new cases but exposed two old tests hardcoding version 5; those now assert version 6, with a separate explicit version-5 load check. `git diff --check` passed for changed source/docs.

Build/capture deviation: the MCP handshake and `read_ak_state` succeeded; `run_ak_animation_test` was attempted. The standard build could not overwrite the executable used by the open editor. Built the same Debug target into `build/additive-bin` with MSBuild `OutDir` and disabled its default post-build copy (the running editor also locks Scripting files). Copied runtime dependencies into the isolated folder and used the prescribed PowerShell capture runner with its supported `EnginePath` option. The existing editor was not terminated. The default Debug executable remains the running older build; rebuild it after closing that editor to use these changes there.

Both refreshed PNGs were visually inspected. They show the existing AK neutral/angled integration fixture, not a before/after cinematic or temporal proof. The additive regression cases are numeric checks and do not produce individual screenshots. Grip errors:

| Pose | Left error (m) | Right error (m) |
| --- | ---: | ---: |
| Neutral | 1.86265e-08 | 6.00685e-08 |
| Aim, pitch 20 / yaw 15 degrees | 2.42573e-07 | 1.29531e-07 |

Pre-existing review PNGs were backed up to `build/additive-backup` before replacement. Unrelated Unity template changes and AnimationSetup were left intact. No source FBX/material writes were made. No offline caching, fractional masks, new recoil assets or general retargeter were added; these remain outside this engine-layer overhaul.
