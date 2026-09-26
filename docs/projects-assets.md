# Projects and assets

## Project layout

A TheEngine project is a directory with `Assets/`, `Library/` and `ProjectSettings/ProjectSettings.txt`. `Assets/` is user content; `Library/` holds generated script assemblies and caches and can be regenerated. The settings file stores the project name, engine version and last scene. The launcher can create Sample 3D or Empty 3D projects, open existing projects and maintain a recent-project list in the user's app-data directory. `TheEngine.exe --project <folder>` opens one directly.

`C:\Users\nickr\Desktop\AnimationSetup` is the editor project for the AK work. It opens `Assets/Scenes/AK_Aiming.scene`, which assigns `Assets/AK/Rigs/AE_AK.rig` and `Assets/Animators/AK_Aim.controller` to the Quantum player and sockets the AE AK weapon to `vb_ak_weapon`. The controller's Base Layer uses the existing locomotion clips and supplies root motion, pelvis and legs. A full-weight override layer uses the AE first-person AK clip through `Assets/AK/Masks/UpperBody.mask`, which begins at `spine_01` and also includes the three animated IK target bones used by the rig. `AKAimController.cs` drives locomotion with WASD, walk with Left Ctrl, forward run with Left Shift, body turn with Q/E, and spine aim with right mouse or I/J/K/L. The original `Assets/Scenes/AnimationSetup.scene`, `Player.controller` and `PlayerController.cs` remain available. Run `tools/Install-AkAnimationSetup.ps1` to recreate the AK scene and `tools/Test-AkAnimationSetup.ps1` to capture the actual project during movement and movement plus aim.

The AK rig rotates the left clavicle forward before Two Bone IK so the authored support-hand grip remains within arm reach. Its `twobone` controls solve both arms; keep the Animator's older `handIk` option disabled in this scene, or that second pass will pull the hands away from the weapon when the spine aims.

## Asset types and references

The Project window shows files in `Assets/`, supports search, import, rename, drag-to-move and thumbnail previews. Mesh references use a built-in primitive name or `Assets/.../model.ext#<mesh index>`. Materials use `.mat`; scenes use `.scene`; prefabs use `.prefab`; TheEngine Animator Controllers use `.controller`. These text formats are engine-specific even when their names resemble Unity assets.

`ResourceCache` in `src/render/Resources.cpp` loads glTF and FBX models, textures and materials. It caches resources by path and checks file timestamps so edits can reload. FBX animation import and skeleton binding live in `src/anim/`. A model import can create material files for its parts. Assets should remain under the project root and be referenced with project-relative paths; moving a file without updating scene/controller references can leave a missing resource.

## Build outputs versus source

The engine executable and SPIR-V shaders are built under `build/`. C# assemblies and temporary imports are written under a project's `Library/`. Neither location is a replacement for authored `Assets/` files. The repository `.gitignore` excludes local build outputs and local project assets; a separate project repository should ignore its own `Library/` while tracking its authored `Assets/` and `ProjectSettings/`.
