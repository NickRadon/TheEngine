#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "sky.glsl"

// Renders the procedural sky into one face of the environment cube map (source for reflections).
layout(push_constant) uniform Push
{
    vec4 face; // x = face index
} pc;

layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 dir = normalize(CubeFaceDir(int(pc.face.x), inNdc * 0.5 + 0.5));
    vec3 col = SkyRadiance(dir) * u.groundColor.w;
    outColor = vec4(min(col, vec3(64.0)), 1.0); // clamp the sun so prefiltering doesn't sparkle
}
