// Procedural sky: single-scattering Rayleigh/Mie atmosphere, sun disk, stars and clouds.
// Requires common.glsl to be included first.

const float PI = 3.14159265359;
const float R_PLANET = 6371e3;
const float R_ATMOS = 6471e3;
const float VIEW_ALTITUDE = 200.0;
const vec3  BETA_R = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float BETA_M = 21e-6;
const float H_R = 8.0e3;
const float H_M = 1.2e3;
const float SUN_SCALE = 22.0;

vec2 RaySphere(vec3 ro, vec3 rd, float r)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0) return vec2(1e5, -1e5);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

vec3 RayleighBeta() { return BETA_R * u.skyTint.w * (1.5 - u.skyTint.rgb); }

// Transmittance from the viewer towards direction `dir` (used for sun color at ground level).
vec3 Transmittance(vec3 dir, int steps)
{
    vec3 ro = vec3(0.0, R_PLANET + VIEW_ALTITUDE, 0.0);
    dir.y = max(dir.y, -0.02);
    dir = normalize(dir);
    float len = RaySphere(ro, dir, R_ATMOS).y;
    float ds = len / float(steps);
    float odR = 0.0, odM = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        float h = length(ro + dir * (float(i) + 0.5) * ds) - R_PLANET;
        odR += exp(-h / H_R) * ds;
        odM += exp(-h / H_M) * ds;
    }
    return exp(-(RayleighBeta() * odR + BETA_M * 1.1 * odM));
}

// In-scattered radiance along a view ray.
vec3 Atmosphere(vec3 rd, vec3 sunDir, float sunIntensity, float mieG, int iSteps, int jSteps)
{
    vec3 ro = vec3(0.0, R_PLANET + VIEW_ALTITUDE, 0.0);
    float tEnd = RaySphere(ro, rd, R_ATMOS).y;
    float ds = tEnd / float(iSteps);

    vec3 betaR = RayleighBeta();
    float mu = dot(rd, sunDir);
    float gg = mieG * mieG;
    float pR = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float pM = 3.0 / (8.0 * PI) * ((1.0 - gg) * (1.0 + mu * mu)) /
               ((2.0 + gg) * pow(max(1.0 + gg - 2.0 * mu * mieG, 1e-4), 1.5));

    vec3 totalR = vec3(0.0), totalM = vec3(0.0);
    float odR = 0.0, odM = 0.0;
    for (int i = 0; i < iSteps; ++i)
    {
        vec3 pos = ro + rd * (float(i) + 0.5) * ds;
        float h = length(pos) - R_PLANET;
        float stepR = exp(-h / H_R) * ds;
        float stepM = exp(-h / H_M) * ds;
        odR += stepR;
        odM += stepM;

        // Planet shadow: the ray towards the sun hits the ground in front of this sample.
        // (RaySphere returns x > y when there is no intersection, which must not count as shadow.)
        vec2 ground = RaySphere(pos, sunDir, R_PLANET);
        if (ground.x > 0.0 && ground.y >= ground.x) continue;

        float jLen = RaySphere(pos, sunDir, R_ATMOS).y;
        float dsj = jLen / float(jSteps);
        float odRj = 0.0, odMj = 0.0;
        for (int j = 0; j < jSteps; ++j)
        {
            float hj = length(pos + sunDir * (float(j) + 0.5) * dsj) - R_PLANET;
            odRj += exp(-hj / H_R) * dsj;
            odMj += exp(-hj / H_M) * dsj;
        }
        vec3 attn = exp(-(BETA_M * 1.1 * (odM + odMj) + betaR * (odR + odRj)));
        totalR += stepR * attn;
        totalM += stepM * attn;
    }
    return sunIntensity * (pR * betaR * totalR + pM * BETA_M * totalM);
}

float Hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float ValueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 w = f * f * (3.0 - 2.0 * f);
    float a = Hash12(i);
    float b = Hash12(i + vec2(1, 0));
    float c = Hash12(i + vec2(0, 1));
    float d = Hash12(i + vec2(1, 1));
    return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}

float Fbm(vec2 p)
{
    float sum = 0.0, amp = 0.5;
    mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 5; ++i)
    {
        sum += amp * ValueNoise(p);
        p = rot * p * 2.03;
        amp *= 0.5;
    }
    return sum;
}

vec3 Stars(vec3 rd, float time)
{
    vec3 p = rd * 220.0;
    vec3 cell = floor(p);
    float h = Hash13(cell);
    if (h < 0.992) return vec3(0.0);
    vec3 center = cell + 0.5 + (vec3(Hash13(cell + 7.1), Hash13(cell + 3.7), Hash13(cell + 1.3)) - 0.5) * 0.6;
    float d = length(p - center);
    float twinkle = 0.6 + 0.4 * sin(time * (2.0 + h * 6.0) + h * 100.0);
    float intensity = smoothstep(0.35, 0.0, d) * twinkle * (h - 0.992) * 125.0;
    vec3 tint = mix(vec3(1.0, 0.8, 0.6), vec3(0.7, 0.8, 1.0), Hash13(cell + 11.0));
    return tint * intensity;
}

// Full sky radiance for a view direction (atmosphere, stars, sun disk, clouds, ground).
vec3 SkyRadiance(vec3 rd)
{
    vec3 sunDir = normalize(u.sunDir.xyz);
    float sunIntensity = SUN_SCALE * u.sunDir.w;
    float time = u.sunParams.z;

    vec3 viewDir = normalize(vec3(rd.x, max(rd.y, 0.0), rd.z) + vec3(0.0, 1e-3, 0.0));
    vec3 col = Atmosphere(viewDir, sunDir, sunIntensity, 0.758, 16, 6);
    vec3 sunLight = u.sunTransmittance.rgb * sunIntensity * max(sunDir.y + 0.05, 0.0);

    // Stars fade in as the sun sets.
    float night = clamp(-sunDir.y * 5.0 + 0.3, 0.0, 1.0);
    if (rd.y > 0.0 && night > 0.0 && u.sunParams.w > 0.0)
        col += Stars(rd, time) * night * u.sunParams.w * smoothstep(0.0, 0.1, rd.y);

    // Sun disk (Unity's Sun Size / Sun Size Convergence).
    float sunSize = max(u.sunParams.x, 1e-4);
    float convergence = max(u.sunParams.y, 1.0);
    float angle = acos(clamp(dot(rd, sunDir), -1.0, 1.0));
    float radius = sunSize * 0.35;
    float disk = 1.0 - smoothstep(radius, radius * (1.0 + 4.0 / convergence), angle);
    if (disk > 0.0 && rd.y > -0.01)
        col += disk * Transmittance(rd, 6) * sunIntensity * 4.0;

    // Clouds on a virtual plane.
    float coverage = u.cloudParams.x;
    if (coverage > 0.0 && rd.y > 0.0)
    {
        vec2 uv = rd.xz / (rd.y + 0.08) * u.cloudParams.w;
        uv += vec2(time * u.cloudParams.z * 0.02, time * u.cloudParams.z * 0.007);
        float n = Fbm(uv);
        float c = smoothstep(1.0 - coverage, 1.0 - coverage + 0.35, n) * u.cloudParams.y;
        c *= smoothstep(0.0, 0.12, rd.y);
        float shade = Fbm(uv + sunDir.xz * 0.15);
        float lit = clamp(0.55 + (n - shade) * 3.0, 0.2, 1.0);
        vec3 cloudCol = sunLight * 0.06 * lit + u.ambientZenith.rgb * 0.9;
        float silver = pow(max(dot(rd, sunDir), 0.0), 8.0) * 2.0;
        cloudCol += sunLight * 0.05 * silver * (1.0 - c);
        col = mix(col, cloudCol, c);
    }

    // Ground below the horizon.
    if (rd.y < 0.0)
        col = mix(col, u.ambientGround.rgb, smoothstep(0.0, 0.06, -rd.y));
    return col;
}

// Cheap environment lookup for lighting: hemisphere gradient from CPU-computed sky colors.
vec3 SkyAmbient(vec3 dir)
{
    float y = dir.y;
    vec3 sky = mix(u.ambientHorizon.rgb, u.ambientZenith.rgb, pow(clamp(y, 0.0, 1.0), 0.6));
    vec3 ground = mix(u.ambientHorizon.rgb * 0.5 + u.ambientGround.rgb * 0.5, u.ambientGround.rgb, clamp(-y * 4.0, 0.0, 1.0));
    return y >= 0.0 ? sky : ground;
}
