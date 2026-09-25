# TheEngine

A small Unity-style game engine/editor written in C++20:

- **Rendering:** Vulkan 1.3 (dynamic rendering + synchronization2), loaded at runtime with volk, so no Vulkan SDK is needed
- **UI:** Dear ImGui (docking branch) with a Unity dark theme and default Unity layout
- **Gizmos:** ImGuizmo (move/rotate/scale/rect/transform + scene orientation gizmo)
- **Animation:** ImAnim (tool button transitions, persp/iso blend, play-mode tint, toasts, time-of-day presets, launcher pages)
- **Sky:** procedural atmosphere (Rayleigh/Mie single scattering), sun disk, clouds, stars, and ambient lighting derived from the sky
- **Lighting:** PBR (GGX) shading, directional/point/spot lights (up to 32), 4-cascade soft sun shadows (stabilized, 16-tap PCF), SSAO, 4x MSAA, ACES tonemapping
- **Materials:** `.mat` assets with albedo/normal/mask textures, tiling and emission; textures are mipmapped and hot-reloaded when changed on disk
- **Models:** glTF 2.0 (`.gltf` / `.glb`) import via cgltf; node hierarchy, meshes, materials and embedded images are brought in
- **Scripting:** C# like Unity (`MonoBehaviour`, `Start`/`Update`, `transform`, `Input`, `Time`, `Debug.Log`, serialized fields in the Inspector), hosted on .NET through hostfxr
- **Projects:** a Unity Hub-style launcher; each project has `Assets/`, `Library/` and `ProjectSettings/`

## Build

Requirements: Visual Studio 2022+ (C++ workload), CMake 3.20+, a GPU driver with Vulkan 1.3, and the .NET SDK (10+) for C# scripting. Without the .NET SDK the engine still builds, but scripting is disabled.

```bash
git clone --recursive https://github.com/NickRadon/TheEngine
cd TheEngine
cmake -S . -B build
cmake --build build --config Release
```

```bash
build/Release/TheEngine.exe
```

This opens the launcher. Create a project (the *Sample 3D* template includes example scripts and a textured material) or add an existing folder. `TheEngine.exe --project <folder>` skips the launcher.

Debug builds enable the Vulkan validation and synchronization-validation layers when the Vulkan SDK is installed; messages appear in the Console panel.

Shaders in `shaders/` are compiled to SPIR-V at build time by `tools/shaderc_mini.cpp`, a small compiler built on the glslang library. glslang's own CLI isn't used because its build requires Python.

## C# scripting

Scripts are ordinary `.cs` files anywhere under `Assets/`. Create one from *Assets > Create > C# Script* or *Add Component > New script*:

```csharp
using TheEngine;

public class Rotator : MonoBehaviour
{
    public Vector3 speed = new Vector3(0, 45, 0);

    void Update()
    {
        transform.Rotate(speed * Time.deltaTime);
        if (Input.GetKeyDown(KeyCode.Space)) Debug.Log("jump!");
    }
}
```

Saving a script triggers a background `dotnet build` into `Library/ScriptAssemblies`. Errors appear in the Console with file and line, and they block Play mode until they're fixed. Public (or `[SerializeField]`) fields show up in the Inspector. Changes made during Play mode are reloaded when you stop.

## Assets

The Project window shows the real `Assets/` folder: a folder tree, thumbnails, search, create/rename/delete (to the Recycle Bin), drag-to-move, and *Show in Explorer*. You can drop files from Explorer to import them. Drag a model into the Scene view or Hierarchy to instantiate it, and a material onto an object to assign it.

## Tests

`TheEngine --selftest` creates a temporary project and drives the editor through injected ImGui input. It covers gizmo drag, undo/redo, marquee selection, orbit, zoom, F framing, play mode, save/load, materials, glTF import, C# scripts and compile errors, and exits with the number of failures. It never touches the OS mouse or keyboard. Add `--capture-dir <folder>` to also save Scene view renders as BMP files.

```bash
ctest --test-dir build -C Release --output-on-failure
```

## Scene view controls (same as Unity)

| Input | Action |
|---|---|
| RMB drag | Look around (flythrough) |
| RMB + W/A/S/D, Q/E | Fly; Q/E move down/up |
| RMB + Shift | Fly faster (with acceleration) |
| RMB + wheel | Change flythrough speed |
| Alt + LMB drag | Orbit around the pivot |
| MMB drag, Alt + MMB, or View tool + LMB | Pan |
| Alt + RMB drag, or wheel | Zoom |
| Arrow keys | Move across the ground plane |
| F, or double-click in the Hierarchy | Frame selection (animated) |
| Q W E R T Y | View / Move / Rotate / Scale / Rect / Transform tool |
| Z / X | Toggle Pivot/Center, Global/Local |
| Ctrl while dragging a gizmo | Increment snapping |
| LMB drag on empty space | Marquee selection (Ctrl/Shift adds) |
| Scene gizmo | Click an axis to snap the view; the label under it toggles Persp/Iso |
| Ctrl+D, Del, F2 | Duplicate, delete, rename |
| Ctrl+Z, Ctrl+Y | Undo, redo |
| Ctrl+Alt+F, Ctrl+Shift+F | Move to view, align with view |
| Ctrl+P, Ctrl+Shift+P, Ctrl+Alt+P | Play/stop, pause, step |
| Ctrl+S | Save scene |

The scene toolbar also has these shading modes: Shaded, Wireframe, Shaded Wireframe and Ambient Occlusion. It also has 2D mode, skybox/grid/gizmo toggles, and the scene camera settings (FOV, dynamic clipping, easing, acceleration, speed).

## Layout

```
src/core       logging, platform helpers (file dialogs, processes), projects
src/launcher   project launcher (Hub)
src/render     Vulkan context, meshes, resource cache (textures/materials/glTF), scene renderer
src/scene      entities, components, hierarchy, text scene + material formats
src/scripting  .NET host, script compilation and native API for C#
src/editor     editor shell, panels, asset browser, scene/game views, Unity-style camera, self-test
scripting/     TheEngine.ScriptCore (the C# engine API)
shaders/       GLSL (sky, lit mesh, shadows, SSAO, grid, selection mask, composite)
external/      third-party sources (imgui, ImGuizmo, ImAnim, glfw, volk, Vulkan-Headers, glm, glslang, stb, cgltf)
```
