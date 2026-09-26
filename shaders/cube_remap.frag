#version 460
#extension GL_GOOGLE_include_directive : require

// Copies a reflection probe capture (rendered with a regular 90 degree camera) into a cube map face,
// resampling it into the cube map's face orientation.
layout(set = 0, binding = 0) uniform sampler2D captured;

layout(push_constant) uniform Push
{
    vec4 face;    // x = face index
    vec4 forward; // capture camera basis
    vec4 right;
    vec4 up;
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

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

void main()
{
    vec3 d = CubeFaceDir(int(pc.face.x), inNdc * 0.5 + 0.5);
    float z = dot(d, pc.forward.xyz);
    vec2 p = vec2(dot(d, pc.right.xyz), dot(d, pc.up.xyz)) / z;
    vec2 uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); // captures use the flipped (Y up) viewport
    outColor = vec4(min(texture(captured, uv).rgb, vec3(64.0)), 1.0);
}
