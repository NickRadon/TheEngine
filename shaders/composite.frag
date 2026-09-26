#version 460
// Final pass: bloom, color grading (white balance, contrast, saturation, filter), vignette, tonemapping,
// gamma and the selection outline. Mirrors Unity URP's "uber" post-processing order.
layout(set = 0, binding = 0) uniform sampler2D hdrTex;
layout(set = 0, binding = 1) uniform sampler2D maskTex;
layout(set = 0, binding = 2) uniform sampler2D bloomTex;

layout(push_constant) uniform Push
{
    vec4 outlineColor; // rgb, a = enabled
    vec4 params;       // x = exposure (incl. post exposure), y = outline width (px), z = tonemapper (0 none, 1 ACES, 2 neutral), w = bloom intensity
    vec4 bloomTint;    // rgb
    vec4 grading;      // x = contrast factor, y = saturation factor, z = color adjustments enabled
    vec4 colorFilter;  // rgb
    vec4 balance;      // xyz = LMS white balance coefficients, w = enabled
    vec4 vignette;     // x = intensity * 3, y = smoothness * 5, z = aspect ratio, w = enabled
    vec4 vignetteColor;
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

vec3 AcesFitted(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Neutral tonemapper (Unity's "Neutral": Hable-style curve with a white point, minimal hue/saturation shift).
vec3 NeutralCurve(vec3 x, float a, float b, float c, float d, float e, float f)
{
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

vec3 Neutral(vec3 x)
{
    const float a = 0.2, b = 0.29, c = 0.24, d = 0.272, e = 0.02, f = 0.3, whiteLevel = 5.3, whiteClip = 1.0;
    vec3 whiteScale = vec3(1.0) / NeutralCurve(vec3(whiteLevel), a, b, c, d, e, f);
    x = NeutralCurve(x * whiteScale, a, b, c, d, e, f) * whiteScale;
    return clamp(x / whiteClip, 0.0, 1.0);
}

const mat3 LIN_2_LMS = mat3(3.90405e-1, 7.08416e-2, 2.31082e-2,
                            5.49941e-1, 9.63172e-1, 1.28021e-1,
                            8.92632e-3, 1.35775e-3, 9.36245e-1);
const mat3 LMS_2_LIN = mat3(2.85847e+0, -2.10182e-1, -4.18120e-2,
                            -1.62879e+0, 1.15820e+0, -1.18169e-1,
                            -2.48910e-2, 3.24281e-4, 1.06867e+0);

void main()
{
    vec2 uv = inNdc * 0.5 + 0.5;
    vec3 col = texture(hdrTex, uv).rgb;
    if (pc.params.w > 0.0) col += texture(bloomTex, uv).rgb * pc.params.w * pc.bloomTint.rgb;
    col *= pc.params.x;

    // Vignette (applied in linear space, like URP).
    if (pc.vignette.w > 0.5)
    {
        vec2 d = abs(uv - 0.5) * pc.vignette.x;
        d.x *= pc.vignette.z;
        float factor = pow(clamp(1.0 - dot(d, d), 0.0, 1.0), pc.vignette.y);
        col *= mix(pc.vignetteColor.rgb, vec3(1.0), factor);
    }

    if (pc.balance.w > 0.5)
    {
        vec3 lms = LIN_2_LMS * col;
        col = LMS_2_LIN * (lms * pc.balance.xyz);
    }
    if (pc.grading.z > 0.5)
    {
        col *= pc.colorFilter.rgb;
        // Contrast around middle grey in log space.
        const float midGrey = 0.18;
        col = exp2((log2(max(col, vec3(1e-6))) - log2(midGrey)) * pc.grading.x + log2(midGrey));
        float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
        col = max(luma + (col - luma) * pc.grading.y, vec3(0.0));
    }

    int tonemapper = int(pc.params.z + 0.5);
    col = tonemapper == 1 ? AcesFitted(col) : tonemapper == 2 ? Neutral(col) : clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));

    if (pc.outlineColor.a > 0.5)
    {
        vec2 texel = 1.0 / vec2(textureSize(maskTex, 0));
        float center = texture(maskTex, uv).r;
        float w = pc.params.y;
        float maxN = 0.0;
        for (int y = -2; y <= 2; ++y)
        {
            for (int x = -2; x <= 2; ++x)
            {
                vec2 o = vec2(x, y) * w * 0.5;
                if (dot(o, o) > w * w + 0.01) continue;
                maxN = max(maxN, texture(maskTex, uv + o * texel).r);
            }
        }
        col = mix(col, pc.outlineColor.rgb, clamp(maxN - center, 0.0, 1.0));
    }
    outColor = vec4(col, 1.0);
}
