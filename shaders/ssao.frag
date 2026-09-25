#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// Screen-space ambient occlusion (normal-oriented hemisphere, 16 samples).
layout(set = 1, binding = 0) uniform sampler2D depthTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out float outAo;

vec3 ViewPosition(vec2 uv)
{
    float depth = textureLod(depthTex, uv, 0.0).r;
    vec4 world = u.invViewProj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    world /= world.w;
    return (u.view * vec4(world.xyz, 1.0)).xyz;
}

void main()
{
    vec2 uv = inNdc * 0.5 + 0.5;
    float depth = textureLod(depthTex, uv, 0.0).r;
    if (depth >= 1.0)
    {
        outAo = 1.0;
        return;
    }

    vec3 P = ViewPosition(uv);
    vec3 N = normalize(textureLod(normalTex, uv, 0.0).xyz);
    float radius = u.aoParams.z;

    // Per-pixel random rotation (interleaved gradient noise), removed by the blur pass.
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 r = vec3(cos(angle), sin(angle), 0.0);
    vec3 T = normalize(r - N * dot(r, N));
    vec3 B = cross(N, T);

    const int SAMPLES = 16;
    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; ++i)
    {
        float fi = float(i);
        float e1 = (fi + 0.5) / float(SAMPLES);
        float e2 = fract(fi * 0.618034);
        float phi = 6.2831853 * e2;
        float sinT = sqrt(e1);
        vec3 dir = vec3(cos(phi) * sinT, sin(phi) * sinT, sqrt(1.0 - e1));
        float scale = mix(0.1, 1.0, e1); // more samples close to the surface
        vec3 samplePos = P + (T * dir.x + B * dir.y + N * dir.z) * radius * scale;

        vec4 clip = u.proj * vec4(samplePos, 1.0);
        vec2 suv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;

        float sceneZ = ViewPosition(suv).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= samplePos.z + 0.025 * radius ? 1.0 : 0.0) * rangeCheck;
    }
    outAo = pow(clamp(1.0 - occlusion / float(SAMPLES), 0.0, 1.0), u.aoParams.y);
}
