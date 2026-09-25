// Shared scene uniform block. Must match SceneUBO in src/render/SceneRenderer.h.
#define MAX_LIGHTS 32

// Point / spot / extra directional lights. Must match GpuLight in SceneRenderer.h.
struct Light
{
    vec4 positionRange;   // xyz = world position, w = range
    vec4 colorIntensity;  // rgb = color, w = intensity
    vec4 directionType;   // xyz = direction the light points, w = type (0 directional, 1 point, 2 spot)
    vec4 spot;            // x = cos(outer half angle), y = cos(inner half angle)
};

layout(set = 0, binding = 0) uniform SceneUBO
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invViewProj;
    vec4 cameraPos;      // xyz = position, w = 1 if orthographic
    vec4 sunDir;         // xyz = direction towards the sun, w = sun intensity
    vec4 sunColor;       // rgb = light color, w = ambient intensity
    vec4 skyTint;        // rgb = tint, w = atmosphere thickness
    vec4 groundColor;    // rgb = ground color, w = exposure
    vec4 sunParams;      // x = sun size, y = sun convergence, z = time, w = stars intensity
    vec4 cloudParams;    // x = coverage, y = density, z = speed, w = height scale
    vec4 gridParams;     // x = plane (0 xz, 1 xy, 2 yz), y = opacity, z = enabled, w = unused
    vec4 ambientZenith;  // sky radiance straight up (computed on the CPU once per frame)
    vec4 ambientHorizon; // sky radiance at the horizon
    vec4 ambientGround;  // radiance of the virtual ground below the horizon
    vec4 sunTransmittance; // atmosphere transmittance towards the sun (sun color at ground level)
    mat4 shadowMatrices[4]; // world -> light clip space per cascade
    vec4 cascadeSplits;     // view-space far distance of each cascade
    vec4 cascadeTexel;      // world-space shadow texel size per cascade
    vec4 shadowParams;      // x = strength, y = enabled
    vec4 aoParams;          // x = enabled, y = intensity, z = radius, w = direct lighting strength
    vec4 viewportSize;      // xy = size in pixels, zw = 1 / size
    ivec4 lightCount;       // x = number of entries in lights[]
    Light lights[MAX_LIGHTS];
} u;

layout(set = 0, binding = 1) uniform sampler2DArrayShadow shadowMap;
layout(set = 0, binding = 2) uniform sampler2D aoTex; // screen-space ambient occlusion (1 = unoccluded)

// Fullscreen-triangle helper: returns NDC for vertex 0..2.
vec2 FullscreenNdc(int index)
{
    vec2 uv = vec2((index << 1) & 2, index & 2);
    return uv * 2.0 - 1.0;
}

// World-space ray through an NDC position (works for perspective and orthographic).
void NdcRay(vec2 ndc, out vec3 origin, out vec3 dir)
{
    vec4 nearP = u.invViewProj * vec4(ndc, 0.0, 1.0);
    vec4 farP  = u.invViewProj * vec4(ndc, 1.0, 1.0);
    nearP.xyz /= nearP.w;
    farP.xyz  /= farP.w;
    origin = nearP.xyz;
    dir = normalize(farP.xyz - nearP.xyz);
}
