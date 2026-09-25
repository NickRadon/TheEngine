#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "sky.glsl"

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 origin, dir;
    NdcRay(inNdc, origin, dir);
    vec3 col = SkyRadiance(dir) * u.groundColor.w;
    outColor = vec4(col, 1.0);
}
