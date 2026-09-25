#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "push.glsl"

// Depth-only shadow caster pass; params.w selects the cascade.
layout(location = 0) in vec3 inPos;

void main()
{
    gl_Position = u.shadowMatrices[int(pc.params.w)] * pc.model * vec4(inPos, 1.0);
}
