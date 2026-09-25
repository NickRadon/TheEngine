#version 460
// Tonemapping, gamma and selection outline.
layout(set = 0, binding = 0) uniform sampler2D hdrTex;
layout(set = 0, binding = 1) uniform sampler2D maskTex;

layout(push_constant) uniform Push
{
    vec4 outlineColor; // rgb, a = enabled
    vec4 params;       // x = exposure, y = outline width (px)
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

vec3 AcesFitted(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec2 uv = inNdc * 0.5 + 0.5;
    vec3 hdr = texture(hdrTex, uv).rgb * pc.params.x;
    vec3 col = pow(AcesFitted(hdr), vec3(1.0 / 2.2));

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
