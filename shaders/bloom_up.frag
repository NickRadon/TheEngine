#version 460

// Bloom upsample: 3x3 tent filter of the smaller mip, added (blend ONE, ONE) onto the next larger mip.
layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push
{
    vec4 params; // xy = source texel size, z = scatter
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = inNdc * 0.5 + 0.5;
    vec2 t = pc.params.xy;
    vec3 col = textureLod(source, uv, 0.0).rgb * 4.0;
    col += (textureLod(source, uv + vec2(-t.x, 0), 0.0).rgb + textureLod(source, uv + vec2(t.x, 0), 0.0).rgb +
            textureLod(source, uv + vec2(0, -t.y), 0.0).rgb + textureLod(source, uv + vec2(0, t.y), 0.0).rgb) * 2.0;
    col += textureLod(source, uv + vec2(-t.x, -t.y), 0.0).rgb + textureLod(source, uv + vec2(t.x, -t.y), 0.0).rgb +
           textureLod(source, uv + vec2(-t.x, t.y), 0.0).rgb + textureLod(source, uv + vec2(t.x, t.y), 0.0).rgb;
    outColor = vec4(col / 16.0 * pc.params.z, 1.0);
}
