# Lighting, sky, reflections and volumes

## Direct lights and shadows

Scene entities can have directional, point or spot Light components. The first active directional light supplies the procedural sky's sun direction and the cascaded sun shadow. Light color, intensity and shadow settings are edited in the Inspector. Point and spot lights also have range; spot lights have inner and outer cone angles.

The renderer shades with a metallic/roughness GGX model. Directional shadows use four stabilized cascades on a 2048-pixel depth array with soft sampling. Local shadows use a separate 1024-pixel depth array: one layer per spot light or six cube faces per point light. The local shadow array has 24 layers per view, so shadow casting lights compete for that budget. A mesh can disable shadow casting or render as shadows only; shadows-only meshes remain in shadow passes but are hidden from ordinary camera color passes.

## Sky and reflections

`SceneRenderer.cpp` evaluates the scene's procedural atmosphere, sun disk, clouds, stars and ambient color. The sky is captured into a cube map and GGX-prefiltered for environment reflections. A Reflection Probe component captures six scene views at its position and uses its box to determine where that cube map applies. Box projection supplies parallax correction for rooms. Probe captures refresh when the relevant scene or lighting state changes; `SceneRendererFx.cpp` owns the capture and filtering path.

Add a probe through **GameObject > Light > Reflection Probe**, place it inside the area it should represent, and size its box. Probe intensity and sky reflection intensity are independent controls. If the renderer has no free probe slot it logs a warning instead of silently baking an extra probe.

## Post-processing volumes

A Volume can be global or a local box. Enabled override groups contain bloom, color adjustments, white balance, vignette and tonemapping. Local volumes fade with camera distance across `blendDistance`; priority and weight control blending when volumes overlap. Tonemapping offers None, ACES and Neutral. Bloom has intensity, threshold, scatter and tint. The Scene view can toggle post-processing for inspection without changing the scene asset.

Volume data is stored with scene entities in `Scene.h`/`Scene.cpp`; selection and blending happen in the render path. The self-test compares rendered captures with post-processing on and off and checks local volume blending. This system resembles a subset of Unity URP Volumes; it does not load URP volume profiles directly.
