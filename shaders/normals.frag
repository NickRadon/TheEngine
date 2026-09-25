#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// Depth/normal prepass for SSAO: view-space normals.
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 0) out vec4 outNormal;

void main()
{
    vec3 n = normalize(inNormal);
    if (!gl_FrontFacing) n = -n;
    outNormal = vec4(normalize(mat3(u.view) * n), 1.0);
}
