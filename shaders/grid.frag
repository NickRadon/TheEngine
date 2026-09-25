#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// Infinite editor grid with adaptive minor/major lines and colored axes.
layout(location = 0) in vec2 inNdc;
layout(location = 0) out vec4 outColor;

vec2 PlaneCoords(vec3 p, int plane)
{
    if (plane == 1) return p.xy;
    if (plane == 2) return p.zy;
    return p.xz;
}

float GridLines(vec2 coord, float scale)
{
    vec2 c = coord / scale;
    vec2 d = fwidth(c);
    vec2 g = abs(fract(c - 0.5) - 0.5) / max(d, vec2(1e-5));
    float line = min(g.x, g.y);
    // Fade lines out once they get denser than a few pixels apart (avoids moire and a bright haze).
    float density = clamp(max(d.x, d.y) * 6.0, 0.0, 1.0);
    return (1.0 - min(line, 1.0)) * (1.0 - density);
}

void main()
{
    vec3 origin, dir;
    NdcRay(inNdc, origin, dir);

    int plane = int(u.gridParams.x + 0.5);
    vec3 n = plane == 1 ? vec3(0, 0, 1) : (plane == 2 ? vec3(1, 0, 0) : vec3(0, 1, 0));
    float denom = dot(dir, n);
    if (abs(denom) < 1e-6) discard;
    float t = -dot(origin, n) / denom;
    if (t <= 0.0) discard;

    vec3 p = origin + dir * t;
    vec4 clip = u.viewProj * vec4(p, 1.0);
    float depth = clip.z / clip.w;
    if (depth < 0.0 || depth > 1.0) discard;
    gl_FragDepth = depth;

    vec2 coord = PlaneCoords(p, plane);

    // Adaptive grid level based on camera height above the plane (or ortho size).
    float camDist = u.cameraPos.w > 0.5 ? 2.0 / u.proj[1][1] : abs(dot(u.cameraPos.xyz, n));
    float logDist = log(max(camDist, 1.0) / 4.0) / log(10.0);
    float lvl = max(floor(logDist), 0.0);
    float scaleA = pow(10.0, lvl);
    float scaleB = scaleA * 10.0;
    float blend = camDist < 4.0 ? 0.0 : fract(logDist);

    float minor = GridLines(coord, scaleA) * (1.0 - blend);
    float major = GridLines(coord, scaleB);

    float alpha = max(minor * 0.25, major * 0.45);
    vec3 col = vec3(0.55);

    // Axis lines.
    vec2 dA = fwidth(coord);
    float axisU = 1.0 - min(abs(coord.y) / max(dA.y, 1e-5) / 1.5, 1.0);
    float axisV = 1.0 - min(abs(coord.x) / max(dA.x, 1e-5) / 1.5, 1.0);
    vec3 red = vec3(1.0, 0.3, 0.3), green = vec3(0.45, 0.9, 0.3), blue = vec3(0.25, 0.45, 1.0);
    vec3 colU = plane == 2 ? blue : red;
    vec3 colV = plane == 0 ? blue : green;
    if (axisU > 0.0) { col = mix(col, colU, axisU); alpha = max(alpha, axisU * 0.9); }
    if (axisV > 0.0) { col = mix(col, colV, axisV); alpha = max(alpha, axisV * 0.9); }

    // Fade with distance and grazing angle.
    float fadeDist = u.cameraPos.w > 0.5 ? 1.0 : 1.0 - smoothstep(camDist * 8.0, camDist * 30.0 + 40.0, t);
    float grazing = smoothstep(0.0, 0.15, abs(denom));
    alpha *= fadeDist * grazing * u.gridParams.y;
    if (alpha < 0.005) discard;
    outColor = vec4(col, alpha);
}
