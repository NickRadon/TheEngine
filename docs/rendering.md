# Rendering and materials

## Frame path

`VulkanContext.cpp` initializes Vulkan 1.3 through volk. `SceneRenderer.cpp` prepares per-view targets, shadows, scene data and draw calls; `SceneRendererFx.cpp` handles local shadows, reflection captures and bloom. The renderer uses dynamic rendering and synchronization2. Scene and Game views have their own render targets. Debug builds enable Vulkan validation when the SDK layers are installed.

Meshes can be built-in primitives or indexed parts of an imported glTF/FBX model (`Assets/.../model.fbx#0`). Static meshes and skinned meshes take different draw paths. Animator joint matrices are sent to GPU skinning. Scene view modes include shaded, wireframe, shaded wireframe and ambient occlusion. The scene camera can toggle its skybox, grid, gizmos and effects.

The main shading path uses GGX PBR, sky/probe reflections, direct lights, shadows and SSAO. Multisample anti-aliasing is selected from device support, with the renderer resolving into its HDR target before final composition. Tone mapping and color effects then produce the display image. Captures written by the engine are BMP snapshots of the final view.

## Material assets and textures

`.mat` assets use `MaterialAsset` in `src/scene/Material.h`. They store albedo color/map, normal map and strength, a mask map, metallic, smoothness, tiling and emission. The mask convention is glTF-like: green is roughness and blue is metallic, multiplied by material factors. An empty material path uses the renderer's default material; mesh renderers also have inline color/metallic/smoothness values.

`ResourceCache` loads textures, materials and models by project-relative path, uploads GPU resources, creates Project-window thumbnails and checks timestamps for reload. Texture imports generate mipmaps. glTF and FBX import can create material assets for model parts. When changing a material format or cache key, update both the asset loader and editor inspector so a saved edit matches the next reload.

## Shaders and limits

GLSL sources live in `shaders/`. CMake invokes `tools/shaderc_mini.cpp` to compile them to SPIR-V under `build/shaders/`. Shader output and GPU caches are build products, not project assets. The current pipeline is a built-in engine renderer; it does not execute Unity shaders, Shader Graph or VFX Graph assets.
