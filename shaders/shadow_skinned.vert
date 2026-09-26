#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "push.glsl"

// Skinned shadow caster (see shadow.vert).
layout(location = 0) in vec3 inPos;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(std430, set = 2, binding = 0) readonly buffer JointMatrices { mat4 joints[]; };

void main()
{
    mat4 skin = inWeights.x * joints[inJoints.x] + inWeights.y * joints[inJoints.y] +
                inWeights.z * joints[inJoints.z] + inWeights.w * joints[inJoints.w];
    int layer = int(pc.params.w + 0.5);
    mat4 m = layer < 4 ? u.shadowMatrices[layer] : u.localShadowMatrices[layer - 4];
    gl_Position = m * pc.model * (skin * vec4(inPos, 1.0));
}
