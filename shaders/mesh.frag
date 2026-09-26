#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "sky.glsl"
#include "push.glsl"

// Material textures (Unity Standard-like metallic workflow).
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D maskMap; // g = roughness multiplier, b = metallic multiplier

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 0) out vec4 outColor;

float DistributionGGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Tangent frame from screen-space derivatives, so meshes don't need per-vertex tangents.
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv)
{
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return mat3(T * invmax, B * invmax, N);
}

// Cascaded shadow lookup with normal-offset bias and a 4x4 hardware-PCF kernel.
float SunShadow(vec3 worldPos, vec3 N, vec3 L)
{
    if (u.shadowParams.y < 0.5) return 1.0;
    float viewDepth = -(u.view * vec4(worldPos, 1.0)).z;
    if (viewDepth > u.cascadeSplits.w) return 1.0;

    int c = viewDepth < u.cascadeSplits.x ? 0 : viewDepth < u.cascadeSplits.y ? 1 : viewDepth < u.cascadeSplits.z ? 2 : 3;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 offsetPos = worldPos + N * u.cascadeTexel[c] * (2.0 - NdotL);
    vec4 sp = u.shadowMatrices[c] * vec4(offsetPos, 1.0);
    vec3 s = sp.xyz / sp.w;
    vec2 uv = s.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float lit = 0.0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            lit += texture(shadowMap, vec4(uv + (vec2(x, y) - 1.5) * texel, float(c), s.z));
    lit /= 16.0;

    float fade = smoothstep(u.cascadeSplits.w * 0.85, u.cascadeSplits.w, viewDepth);
    return mix(mix(1.0, lit, u.shadowParams.x), 1.0, fade);
}

// Point/spot light shadows: spot lights use one layer, point lights six cube-face layers.
float LocalShadow(Light light, int type, vec3 worldPos, vec3 N)
{
    if (light.spot.z < -0.5) return 1.0;
    int layer = int(light.spot.z + 0.5);
    vec3 d = worldPos - light.positionRange.xyz;
    if (type == 1)
    {
        vec3 a = abs(d);
        int face = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1) : (a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5));
        layer += face;
    }
    // Normal offset scaled with the texel footprint at this distance.
    vec2 size = vec2(textureSize(localShadowMap, 0).xy);
    float texelWorld = length(d) * 2.4 / size.x;
    vec3 L = normalize(-d);
    vec3 p = worldPos + N * texelWorld * (1.5 - clamp(dot(N, L), 0.0, 1.0));
    vec4 sp = u.localShadowMatrices[layer] * vec4(p, 1.0);
    if (sp.w <= 0.0) return 1.0;
    vec3 s = sp.xyz / sp.w;
    vec2 uv = s.xy * 0.5 + 0.5;
    vec2 texel = 1.0 / size;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(localShadowMap, vec4(uv + vec2(x, y) * texel, float(layer), s.z));
    lit /= 9.0;
    return mix(1.0, lit, light.spot.w);
}

// Reflection probe lookup: the smallest probe box containing the point, else the sky.
vec3 EnvironmentSpecular(vec3 worldPos, vec3 R, float roughness)
{
    float lod = roughness * (u.envParams.x - 1.0);
    int best = -1;
    float bestVolume = 1e30;
    for (int i = 0; i < int(u.envParams.z); ++i)
    {
        Probe pr = u.probes[i];
        if (all(greaterThanEqual(worldPos, pr.boxMin.xyz)) && all(lessThanEqual(worldPos, pr.boxMax.xyz)))
        {
            vec3 e = pr.boxMax.xyz - pr.boxMin.xyz;
            float volume = e.x * e.y * e.z;
            if (volume < bestVolume) { bestVolume = volume; best = i; }
        }
    }
    if (best < 0) return textureLod(envCubes, vec4(R, 0.0), lod).rgb * u.envParams.y;

    Probe pr = u.probes[best];
    vec3 dir = R;
    if (pr.boxMax.w > 0.5)
    {
        // Box projection: intersect the reflection ray with the probe box (parallax-corrected cubemap).
        vec3 t1 = (pr.boxMax.xyz - worldPos) / R;
        vec3 t2 = (pr.boxMin.xyz - worldPos) / R;
        vec3 tf = max(t1, t2);
        float t = min(min(tf.x, tf.y), tf.z);
        dir = worldPos + R * t - pr.centerIndex.xyz;
    }
    return textureLod(envCubes, vec4(dir, pr.centerIndex.w), lod).rgb * pr.boxMin.w;
}

// Split-sum environment BRDF, analytic fit (Karis, "Physically Based Shading on Mobile").
vec2 EnvBrdf(float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

struct Surface
{
    vec3 N, V, albedo, F0;
    float roughness, metallic;
};

vec3 Brdf(Surface s, vec3 L, vec3 radiance)
{
    vec3 H = normalize(s.V + L);
    float NdotL = max(dot(s.N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    float NdotV = max(dot(s.N, s.V), 1e-3);
    float NdotH = max(dot(s.N, H), 0.0);
    vec3 F = s.F0 + (1.0 - s.F0) * pow(1.0 - max(dot(H, s.V), 0.0), 5.0);
    float D = DistributionGGX(NdotH, s.roughness * s.roughness);
    float k = (s.roughness + 1.0) * (s.roughness + 1.0) / 8.0;
    float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
    vec3 spec = D * F * G / max(4.0 * NdotV * NdotL, 1e-3);
    vec3 kd = (1.0 - F) * (1.0 - s.metallic);
    return (kd * s.albedo / PI + spec) * radiance * NdotL;
}

void main()
{
    if (pc.params.z > 0.5)
    {
        outColor = vec4(pc.color.rgb, 1.0);
        return;
    }

    vec4 albedoSample = texture(albedoMap, inUv);
    vec4 mask = texture(maskMap, inUv);

    Surface s;
    vec3 N = normalize(inNormal);
    if (!gl_FrontFacing) N = -N;
    if (pc.extra.w > 0.5)
    {
        vec3 tn = texture(normalMap, inUv).xyz * 2.0 - 1.0;
        tn.xy *= pc.extra.z;
        N = normalize(CotangentFrame(N, inWorldPos, inUv) * tn);
    }
    s.N = N;
    vec3 camBackward = normalize(vec3(u.view[0][2], u.view[1][2], u.view[2][2]));
    s.V = u.cameraPos.w > 0.5 ? camBackward : normalize(u.cameraPos.xyz - inWorldPos);
    s.albedo = pc.color.rgb * albedoSample.rgb;
    s.metallic = clamp(pc.params.x * mask.b, 0.0, 1.0);
    s.roughness = clamp((1.0 - pc.params.y) * mask.g, 0.04, 1.0);
    s.F0 = mix(vec3(0.04), s.albedo, s.metallic);

    // Ambient occlusion from the SSAO pass (screen space).
    float ao = 1.0;
    if (u.aoParams.x > 0.5)
        ao = texture(aoTex, gl_FragCoord.xy * u.viewportSize.zw).r;
    if (u.aoParams.x > 1.5)
    {
        outColor = vec4(vec3(ao * 0.9), 1.0); // Scene view "Ambient Occlusion" draw mode
        return;
    }

    // Sun
    vec3 L = normalize(u.sunDir.xyz);
    vec3 sunRadiance = u.sunColor.rgb * u.sunDir.w * u.sunTransmittance.rgb * PI * smoothstep(-0.05, 0.05, L.y);
    vec3 direct = Brdf(s, L, sunRadiance) * SunShadow(inWorldPos, N, L);

    // Point / spot / additional directional lights
    for (int i = 0; i < u.lightCount.x; ++i)
    {
        Light light = u.lights[i];
        int type = int(light.directionType.w + 0.5);
        vec3 Ll;
        float atten = 1.0;
        if (type == 0)
        {
            Ll = -normalize(light.directionType.xyz);
        }
        else
        {
            vec3 d = light.positionRange.xyz - inWorldPos;
            float dist = length(d);
            Ll = d / max(dist, 1e-4);
            float range = max(light.positionRange.w, 1e-3);
            float window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            atten = window * window / (dist * dist + 1.0);
            if (type == 2)
            {
                float cd = dot(-Ll, normalize(light.directionType.xyz));
                atten *= smoothstep(light.spot.x, light.spot.y, cd);
            }
        }
        if (atten > 0.0 && type != 0) atten *= LocalShadow(light, type, inWorldPos, N);
        if (atten > 0.0)
            direct += Brdf(s, Ll, light.colorIntensity.rgb * light.colorIntensity.w * PI * atten);
    }

    // Ambient: hemisphere lighting from the sky plus a blurred sky reflection.
    float NdotV = max(dot(N, s.V), 1e-3);
    vec3 skyN = SkyAmbient(N);
    vec3 R = reflect(-s.V, N);
    // Specular: prefiltered sky / reflection probe (roughness picks the mip) with the split-sum BRDF.
    vec2 envBrdf = EnvBrdf(s.roughness, NdotV);
    vec3 specularAmbient = EnvironmentSpecular(inWorldPos, R, s.roughness) * (s.F0 * envBrdf.x + envBrdf.y);
    vec3 Fa = s.F0 + (max(vec3(1.0 - s.roughness), s.F0) - s.F0) * pow(1.0 - NdotV, 5.0);
    vec3 kdA = (1.0 - Fa) * (1.0 - s.metallic);
    vec3 ambient = (kdA * s.albedo * skyN * u.sunColor.w + specularAmbient) * ao;

    direct *= mix(1.0, ao, u.aoParams.w);
    outColor = vec4(direct + ambient + pc.emission.rgb, 1.0);
}
