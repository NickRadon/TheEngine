#version 460

// Bloom downsample (13-tap, Call of Duty: Advanced Warfare). The first pass also applies the soft threshold.
layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push
{
    vec4 params; // xy = source texel size, z = threshold, w = 1 for the first (prefilter) pass
    vec4 knee;   // x = soft knee
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

vec3 Tap(vec2 uv) { return textureLod(source, uv, 0.0).rgb; }

void main()
{
    vec2 uv = inNdc * 0.5 + 0.5;
    vec2 t = pc.params.xy;
    vec3 a = Tap(uv + t * vec2(-2, -2)), b = Tap(uv + t * vec2(0, -2)), c = Tap(uv + t * vec2(2, -2));
    vec3 d = Tap(uv + t * vec2(-2, 0)),  e = Tap(uv),                    f = Tap(uv + t * vec2(2, 0));
    vec3 g = Tap(uv + t * vec2(-2, 2)),  h = Tap(uv + t * vec2(0, 2)),  i = Tap(uv + t * vec2(2, 2));
    vec3 j = Tap(uv + t * vec2(-1, -1)), k = Tap(uv + t * vec2(1, -1));
    vec3 l = Tap(uv + t * vec2(-1, 1)),  m = Tap(uv + t * vec2(1, 1));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;

    if (pc.params.w > 0.5)
    {
        // Soft-knee threshold on brightness; also tames extreme values (the sun) to avoid flicker.
        col = min(col, vec3(64.0));
        float brightness = max(col.r, max(col.g, col.b));
        float threshold = pc.params.z;
        float knee = threshold * pc.knee.x + 1e-4;
        float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
        col *= contribution;
    }
    outColor = vec4(col, 1.0);
}
