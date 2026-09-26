# Engine systems

These pages describe the implementation in this repository. They distinguish engine features from Unity APIs that have not been implemented. Start with [projects and assets](projects-assets.md) to understand paths and file formats, then use the system page for the component you are changing.

| System | Guide | Main implementation |
| --- | --- | --- |
| Animation and rigging | [animation.md](animation.md) | `src/anim/`, `src/editor/EditorAnimator.cpp` |
| Physics and character movement | [physics.md](physics.md) | `src/physics/`, `scripting/ScriptCore/Physics.cs` |
| Lighting, sky, reflections and volumes | [lighting.md](lighting.md) | `src/render/SceneRenderer.cpp`, `SceneRendererFx.cpp` |
| Rendering and materials | [rendering.md](rendering.md) | `src/render/`, `shaders/` |
| Scenes and prefabs | [scenes-prefabs.md](scenes-prefabs.md) | `src/scene/` |
| C# scripting and input | [scripting-input.md](scripting-input.md) | `src/scripting/`, `scripting/ScriptCore/` |
| Projects and asset import | [projects-assets.md](projects-assets.md) | `src/core/Project.cpp`, `src/render/Resources.cpp` |
| Editor and validation | [editor.md](editor.md) | `src/editor/`, `src/main.cpp` |

The native application is built by CMake from `src/`; `scripting/ScriptCore` supplies the C# API. The project being edited lives outside this repository and contains its own `Assets/`, `Library/` and `ProjectSettings/`. Generated files under `build/` and a project's `Library/` are not source assets.

AnimationSetup retains its original third-person scene and scripts. Its separate `AK_Aiming.scene` now contains the AK rig and aiming preview; `tools/Install-AkAnimationSetup.ps1` recreates that setup from this repository without CAS Demo assets.
