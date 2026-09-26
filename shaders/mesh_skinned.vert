#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "push.glsl"

// Skinned variant of mesh.vert: up to 4 joint influences, joint matrices from a storage buffer (set 2).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(std430, set = 2, binding = 0) readonly buffer JointMatrices { mat4 joints[]; };

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;

void main()
{
    mat4 skin = inWeights.x * joints[inJoints.x] + inWeights.y * joints[inJoints.y] +
                inWeights.z * joints[inJoints.z] + inWeights.w * joints[inJoints.w];
    vec4 world = pc.model * (skin * vec4(inPos, 1.0));
    outWorldPos = world.xyz;
    outNormal = transpose(inverse(mat3(pc.model))) * (mat3(skin) * inNormal);
    outUv = inUv * pc.extra.xy;
    gl_Position = u.viewProj * world;
}
