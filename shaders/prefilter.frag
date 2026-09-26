#version 460

// GGX prefiltering of an environment cube map for one face and mip (roughness = mip / (mips - 1)).
// Uses filtered importance sampling (sample mip chosen from the PDF) to avoid fireflies with few samples.
layout(set = 0, binding = 0) uniform samplerCube source;

layout(push_constant) uniform Push
{
    vec4 params; // x = face, y = roughness, z = source face size, w = unused
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

vec3 CubeFaceDir(int face, vec2 uv)
{
    vec2 st = uv * 2.0 - 1.0;
    if (face == 0) return vec3(1.0, -st.y, -st.x);
    if (face == 1) return vec3(-1.0, -st.y, st.x);
    if (face == 2) return vec3(st.x, 1.0, st.y);
    if (face == 3) return vec3(st.x, -1.0, -st.y);
    if (face == 4) return vec3(st.x, -st.y, 1.0);
    return vec3(-st.x, -st.y, -1.0);
}

vec2 Hammersley(uint i, uint n)
{
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}

void main()
{
    vec3 N = normalize(CubeFaceDir(int(pc.params.x), inNdc * 0.5 + 0.5));
    float roughness = pc.params.y;
    if (roughness <= 0.0)
    {
        outColor = vec4(textureLod(source, N, 0.0).rgb, 1.0);
        return;
    }

    float a = roughness * roughness;
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    float texelSolidAngle = 4.0 * PI / (6.0 * pc.params.z * pc.params.z);

    const uint SAMPLES = 96u;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < SAMPLES; ++i)
    {
        vec2 xi = Hammersley(i, SAMPLES);
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 H = normalize(T * (cos(phi) * sinTheta) + B * (sin(phi) * sinTheta) + N * cosTheta);
        vec3 L = 2.0 * dot(N, H) * H - N; // V = N
        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;
        float NdotH = max(dot(N, H), 0.0);
        float d = NdotH * NdotH * (a * a - 1.0) + 1.0;
        float D = a * a / (PI * d * d);
        float pdf = D * 0.25 + 1e-4; // D * NdotH / (4 * VdotH) with V = N
        float sampleSolidAngle = 1.0 / (float(SAMPLES) * pdf);
        float mip = 0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0;
        sum += textureLod(source, L, max(mip, 0.0)).rgb * NdotL;
        weight += NdotL;
    }
    outColor = vec4(sum / max(weight, 1e-4), 1.0);
}
