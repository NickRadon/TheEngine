# Projects and assets

## Project layout

A TheEngine project is a directory with `Assets/`, `Library/` and `ProjectSettings/ProjectSettings.txt`. `Assets/` is user content; `Library/` holds generated script assemblies and caches and can be regenerated. The settings file stores the project name, engine version and last scene. The launcher can create Sample 3D or Empty 3D projects, open existing projects and maintain a recent-project list in the user's app-data directory. `TheEngine.exe --project <folder>` opens one directly.

The clean `C:\Users\nickr\Desktop\AnimationSetup` project uses `Assets/Scenes/AnimationSetup.scene`. Its authored scene, `Player.controller`, `PlayerController.cs` and `ThirdPersonCamera.cs` form the original setup. Character, Locomotion, AK, Materials and Textures are imported source assets. Imported AK files do not imply an AK gameplay system is active in this scene.

## Asset types and references

The Project window shows files in `Assets/`, supports search, import, rename, drag-to-move and thumbnail previews. Mesh references use a built-in primitive name or `Assets/.../model.ext#<mesh index>`. Materials use `.mat`; scenes use `.scene`; prefabs use `.prefab`; TheEngine Animator Controllers use `.controller`. These text formats are engine-specific even when their names resemble Unity assets.

`ResourceCache` in `src/render/Resources.cpp` loads glTF and FBX models, textures and materials. It caches resources by path and checks file timestamps so edits can reload. FBX animation import and skeleton binding live in `src/anim/`. A model import can create material files for its parts. Assets should remain under the project root and be referenced with project-relative paths; moving a file without updating scene/controller references can leave a missing resource.

## Build outputs versus source

The engine executable and SPIR-V shaders are built under `build/`. C# assemblies and temporary imports are written under a project's `Library/`. Neither location is a replacement for authored `Assets/` files. The repository `.gitignore` excludes local build outputs and local project assets; a separate project repository should ignore its own `Library/` while tracking its authored `Assets/` and `ProjectSettings/`.
