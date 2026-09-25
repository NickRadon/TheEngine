#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// Depth-aware 4x4 blur that removes the SSAO rotation noise without bleeding across edges.
layout(set = 1, binding = 0) uniform sampler2D aoRaw;
layout(set = 1, binding = 1) uniform sampler2D depthTex;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out float outAo;

float ViewDepth(vec2 uv)
{
    float depth = textureLod(depthTex, uv, 0.0).r;
    vec4 world = u.invViewProj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    world /= world.w;
    return (u.view * vec4(world.xyz, 1.0)).z;
}

void main()
{
    vec2 uv = inNdc * 0.5 + 0.5;
    vec2 texel = u.viewportSize.zw;
    float centerZ = ViewDepth(uv);
    float tolerance = 0.08 * abs(centerZ) + 0.05;

    float sum = 0.0, weights = 0.0;
    for (int y = -2; y < 2; ++y)
    {
        for (int x = -2; x < 2; ++x)
        {
            vec2 suv = uv + (vec2(x, y) + 0.5) * texel;
            float w = max(0.0, 1.0 - abs(ViewDepth(suv) - centerZ) / tolerance);
            sum += textureLod(aoRaw, suv, 0.0).r * w;
            weights += w;
        }
    }
    outAo = weights > 0.0 ? sum / weights : 1.0;
}
