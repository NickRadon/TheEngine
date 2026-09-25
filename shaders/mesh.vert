#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "push.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;

void main()
{
    vec4 world = pc.model * vec4(inPos, 1.0);
    outWorldPos = world.xyz;
    outNormal = transpose(inverse(mat3(pc.model))) * inNormal;
    outUv = inUv * pc.extra.xy;
    gl_Position = u.viewProj * world;
}
