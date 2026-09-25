// Per-draw push constants (128 bytes). Must match SceneRenderer::PushConstants.
layout(push_constant) uniform Push
{
    mat4 model;
    vec4 color;    // rgb = albedo tint
    vec4 params;   // x = metallic, y = smoothness, z = unlit flag, w = shadow cascade
    vec4 extra;    // xy = uv tiling, z = normal strength, w = has normal map
    vec4 emission; // rgb = emissive color
} pc;
