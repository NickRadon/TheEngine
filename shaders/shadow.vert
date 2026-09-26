#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "push.glsl"

// Depth-only shadow caster pass; params.w selects the cascade.
layout(location = 0) in vec3 inPos;

void main()
{
    // Layers 0-3 are the sun's cascades, 4+ the point/spot light shadow layers.
    int layer = int(pc.params.w + 0.5);
    mat4 m = layer < 4 ? u.shadowMatrices[layer] : u.localShadowMatrices[layer - 4];
    gl_Position = m * pc.model * vec4(inPos, 1.0);
}
