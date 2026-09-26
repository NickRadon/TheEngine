# TheEngine

A small Unity-style game engine/editor written in C++20:

- **Rendering:** Vulkan 1.3 (dynamic rendering + synchronization2), loaded at runtime with volk, so no Vulkan SDK is needed
- **UI:** Dear ImGui (docking branch) with a Unity dark theme and default Unity layout
- **Gizmos:** ImGuizmo (move/rotate/scale/rect/transform + scene orientation gizmo)
- **Animation:** ImAnim (tool button transitions, persp/iso blend, play-mode tint, toasts, time-of-day presets, launcher pages)
- **Sky:** procedural atmosphere (Rayleigh/Mie single scattering), sun disk, clouds, stars, and ambient lighting derived from the sky
- **Lighting:** PBR (GGX) shading, directional/point/spot lights (up to 32), 4-cascade soft sun shadows (stabilized, 16-tap PCF), point/spot light shadows (cube and perspective shadow maps), SSAO, 4x MSAA
- **Reflections:** the sky is captured into a GGX-prefiltered cube map; Reflection Probes capture the scene (optionally box-projected) and re-bake automatically when moved or when the lighting changes
- **Post-processing:** Unity-style Volumes (global or local with blend distance and priority): bloom, color adjustments, white balance, vignette, and ACES/Neutral tonemapping
- **Materials:** `.mat` assets with albedo/normal/mask textures, tiling and emission; textures are mipmapped and hot-reloaded when changed on disk
- **Models:** glTF 2.0 (`.gltf` / `.glb`) via cgltf and FBX via ufbx: meshes, materials, skinned meshes with their skeleton, and animation clips
- **Animation:** Animator component + layered node-based Animator Controllers (states, transitions with conditions/exit time, parameters, 1D/2D blend trees, Any State, avatar masks, override/additive layers), GPU skinning, root motion, look modifiers, bone sockets, and name-based retargeting between rigs that share bone names
- **Scripting:** C# like Unity (`MonoBehaviour`, `Start`/`Update`/`FixedUpdate`, `transform`, `Input`, `Time`, `Debug.Log`, `Instantiate`, serialized fields in the Inspector), hosted on .NET through hostfxr
- **Physics:** Jolt Physics with Unity-style Rigidbody, CharacterController and Box/Sphere/Capsule/Mesh Collider components, triggers, raycasts and collision messages
- **Prefabs:** reusable object hierarchies with per-property overrides, Apply/Revert, and a prefab editing mode
- **Projects:** a Unity Hub-style launcher; each project has `Assets/`, `Library/` and `ProjectSettings/`

The [system guides](docs/README.md) document animation, physics, lighting, rendering, scenes and prefabs, scripting and input, project assets, and editor validation in separate Markdown files.

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

## Lighting, reflections and post-processing

- **Light shadows:** every light has a *Shadow Type* setting. Point lights use 6 shadow layers (cube faces) and spot lights use 1, out of 24 per view.
- **Reflection probes:** add one from *GameObject > Light > Reflection Probe* and size its box. Shiny surfaces inside the box reflect what the probe sees, and everything else reflects the sky. Turn on *Box Projection* for rooms. *Lighting > Reflection Intensity* scales the sky reflections.
- **Volumes:** add one from *GameObject > Volume > Global Volume* (or *Box Volume*) and enable the override groups you want. Volumes blend by priority, weight and, for box volumes, the camera's distance. The Scene view's skybox button also toggles post-processing, like Unity's effects toggle.

## Physics

Add a **Rigidbody** and a collider (Box, Sphere, Capsule or Mesh) from *Add Component*. New primitives get Unity's default collider, and colliders are drawn in green on the selected object. Objects with a collider but no Rigidbody are static. The simulation runs in play mode at a fixed 50 Hz.

```csharp
public class Ball : MonoBehaviour
{
    Rigidbody body;
    void Start() { body = GetComponent<Rigidbody>(); }
    void FixedUpdate() { if (Input.GetKey(KeyCode.Space)) body.AddForce(Vector3.up * 20f); }
    void OnCollisionEnter(Collision c) { Debug.Log("hit " + c.gameObject.name); }
    void OnTriggerEnter(Collider other) { Debug.Log("entered " + other.name); }
}
```

`Physics.Raycast`, `Physics.gravity`, `Rigidbody.velocity`/`AddForce`/`AddTorque` (with `ForceMode`) and `isKinematic` work as in Unity.

## Prefabs

- **Create:** drag an object from the Hierarchy into the Project window (or right-click it and choose *Create Prefab*). Instances show in blue.
- **Place:** drag a `.prefab` into the Scene view or Hierarchy. From C#, add a `public GameObject prefab;` field, drop the prefab on it in the Inspector, and call `Instantiate(prefab, position, rotation)`.
- **Edit:** double-click the prefab (or use *Open* in the Inspector) to edit it on its own, then use the back arrow in the Hierarchy. Changes reach every instance.
- **Overrides:** values you change on an instance are kept when the prefab changes. The Inspector's *Overrides* menu lists them, with *Apply All* and *Revert All*. The root's position, rotation and name always belong to the instance.

## Animation

1. Drop a rigged FBX into the scene. It gets an **Animator** component (bones stay inside the model, not in the Hierarchy).
2. Create an Animator Controller (*Assets > Create > Animator Controller*, or *New* on the Animator component) and open it in the **Animator** window (*Window > Animation > Animator*).
3. In the Animator window:
   - **States:** drag FBX animations onto the graph to make states, or right-click to create empty states and blend trees. The first state becomes the default (orange).
   - **Transitions:** right-click a state > *Make Transition*, then click the target. Select a transition to set its conditions, exit time and blend duration.
   - **Parameters:** add Float/Int/Bool/Trigger parameters on the left. In play mode they show live values, and the playing state shows a progress bar.
   - **Blend trees:** 1D uses thresholds. 2D is freeform cartesian with gradient band interpolation (like Unity).
4. **Root motion:** with *Apply Root Motion* on, the planar movement and turning of the clip's root bone move the object; clips without it play in place. Drive the parameters from C#:

```csharp
Animator animator = GetComponent<Animator>();
animator.SetFloat("MoveY", Input.GetAxis("Vertical") * 2f, 0.1f, Time.deltaTime); // damped
animator.SetTrigger("Jump");
```

Clips are matched to the model's skeleton by bone name. Rotations transfer by name; root/hip translation and authored IK or marker tracks also transfer. This supports related rigs that share bone names, but it is not a general humanoid retargeter.

`TheEngine --project <folder> --playtest <seconds> [--hold W,LeftShift] [--press Space@1.5,Mouse0@3] [--mouse 60,-20] [--capture-dir <folder>]` plays a project with keys held down, keys tapped at given times (so scripts see `GetKeyDown`/`GetKeyUp`), and optional mouse motion in pixels per second. It logs animated objects and saves Game view and editor screenshots, which is useful for checking movement and first-person look without touching the mouse or keyboard.

## Assets

The Project window shows the real `Assets/` folder: a folder tree, thumbnails, search, create/rename/delete (to the Recycle Bin), drag-to-move, and *Show in Explorer*. You can drop files from Explorer to import them. Drag a model into the Scene view or Hierarchy to instantiate it, and a material onto an object to assign it.

## Tests

`TheEngine --selftest` creates a temporary project and drives the editor through injected ImGui input. It covers gizmo drag, undo/redo, marquee selection, orbit, zoom, F framing, play mode, save/load, materials, glTF import, C# scripts and compile errors, physics and collision messages, prefabs and `Instantiate`, animator state machines, blend trees and root motion, and exits with the number of failures. It never touches the OS mouse or keyboard. Add `--capture-dir <folder>` to also save Scene view renders as BMP files.

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
src/scene      entities, components, hierarchy, text scene/material/prefab formats, prefab overrides
src/physics    Jolt Physics world (bodies, contacts, raycasts)
src/anim       FBX import, skeletons, clips, Animator Controller runtime, animation system
src/scripting  .NET host, script compilation and native API for C#
src/editor     editor shell, panels, asset browser, scene/game views, Unity-style camera, self-test
scripting/     TheEngine.ScriptCore (the C# engine API)
shaders/       GLSL (sky, lit mesh, shadows, SSAO, grid, selection mask, composite)
external/      third-party sources (imgui, ImGuizmo, ImAnim, glfw, volk, Vulkan-Headers, glm, glslang, stb, cgltf, JoltPhysics, ufbx) as git submodules
```
