using System;
using System.Globalization;

namespace TheEngine
{
    /// <summary>3D vector (same API subset as UnityEngine.Vector3).</summary>
    public struct Vector3 : IEquatable<Vector3>
    {
        public float x, y, z;

        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        public Vector3(float x, float y) { this.x = x; this.y = y; z = 0; }

        public static Vector3 zero => new Vector3(0, 0, 0);
        public static Vector3 one => new Vector3(1, 1, 1);
        public static Vector3 up => new Vector3(0, 1, 0);
        public static Vector3 down => new Vector3(0, -1, 0);
        public static Vector3 right => new Vector3(1, 0, 0);
        public static Vector3 left => new Vector3(-1, 0, 0);
        /// <summary>Engine forward is -Z (right-handed, Y up).</summary>
        public static Vector3 forward => new Vector3(0, 0, -1);
        public static Vector3 back => new Vector3(0, 0, 1);

        public float magnitude => MathF.Sqrt(x * x + y * y + z * z);
        public float sqrMagnitude => x * x + y * y + z * z;
        public Vector3 normalized { get { float m = magnitude; return m > 1e-6f ? this / m : zero; } }
        public void Normalize() { this = normalized; }

        public float this[int i]
        {
            get => i == 0 ? x : i == 1 ? y : z;
            set { if (i == 0) x = value; else if (i == 1) y = value; else z = value; }
        }

        public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;
        public static Vector3 Cross(Vector3 a, Vector3 b) => new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        public static float Distance(Vector3 a, Vector3 b) => (a - b).magnitude;
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => a + (b - a) * Mathf.Clamp01(t);
        public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t) => a + (b - a) * t;
        public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDelta)
        {
            Vector3 d = target - current;
            float m = d.magnitude;
            return m <= maxDelta || m < 1e-6f ? target : current + d / m * maxDelta;
        }
        public static Vector3 Scale(Vector3 a, Vector3 b) => new Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
        public static Vector3 Min(Vector3 a, Vector3 b) => new Vector3(MathF.Min(a.x, b.x), MathF.Min(a.y, b.y), MathF.Min(a.z, b.z));
        public static Vector3 Max(Vector3 a, Vector3 b) => new Vector3(MathF.Max(a.x, b.x), MathF.Max(a.y, b.y), MathF.Max(a.z, b.z));

        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.x, -a.y, -a.z);
        public static Vector3 operator *(Vector3 a, float s) => new Vector3(a.x * s, a.y * s, a.z * s);
        public static Vector3 operator *(float s, Vector3 a) => a * s;
        public static Vector3 operator /(Vector3 a, float s) => new Vector3(a.x / s, a.y / s, a.z / s);
        public static bool operator ==(Vector3 a, Vector3 b) => (a - b).sqrMagnitude < 1e-10f;
        public static bool operator !=(Vector3 a, Vector3 b) => !(a == b);

        public bool Equals(Vector3 other) => this == other;
        public override bool Equals(object obj) => obj is Vector3 v && this == v;
        public override int GetHashCode() => HashCode.Combine(x, y, z);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F2}, {1:F2}, {2:F2})", x, y, z);
    }

    public struct Vector2
    {
        public float x, y;
        public Vector2(float x, float y) { this.x = x; this.y = y; }
        public static Vector2 zero => new Vector2(0, 0);
        public float magnitude => MathF.Sqrt(x * x + y * y);
        public static Vector2 operator +(Vector2 a, Vector2 b) => new Vector2(a.x + b.x, a.y + b.y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new Vector2(a.x - b.x, a.y - b.y);
        public static Vector2 operator *(Vector2 a, float s) => new Vector2(a.x * s, a.y * s);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F2}, {1:F2})", x, y);
    }

    /// <summary>Rotation quaternion (same API subset as UnityEngine.Quaternion).</summary>
    public struct Quaternion
    {
        public float x, y, z, w;

        public Quaternion(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }
        public static Quaternion identity => new Quaternion(0, 0, 0, 1);

        public static Quaternion AngleAxis(float degrees, Vector3 axis)
        {
            axis = axis.normalized;
            float h = degrees * Mathf.Deg2Rad * 0.5f;
            float s = MathF.Sin(h);
            return new Quaternion(axis.x * s, axis.y * s, axis.z * s, MathF.Cos(h));
        }

        /// <summary>Rotation applied Z, then X, then Y (like Unity), angles in degrees.</summary>
        public static Quaternion Euler(float x, float y, float z) =>
            AngleAxis(y, Vector3.up) * AngleAxis(x, Vector3.right) * AngleAxis(z, new Vector3(0, 0, 1));
        public static Quaternion Euler(Vector3 e) => Euler(e.x, e.y, e.z);

        public static Quaternion LookRotation(Vector3 forward, Vector3 up)
        {
            // Engine forward is -Z, so the camera-style basis is (right, up, -forward).
            Vector3 f = forward.normalized;
            Vector3 r = Vector3.Cross(f, up).normalized;
            if (r.sqrMagnitude < 1e-8f) r = Vector3.Cross(f, Vector3.right).normalized;
            Vector3 u = Vector3.Cross(r, f);
            Vector3 b = -f;
            float m00 = r.x, m01 = u.x, m02 = b.x, m10 = r.y, m11 = u.y, m12 = b.y, m20 = r.z, m21 = u.z, m22 = b.z;
            float trace = m00 + m11 + m22;
            Quaternion q;
            if (trace > 0)
            {
                float s = MathF.Sqrt(trace + 1) * 2;
                q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s = MathF.Sqrt(1 + m00 - m11 - m22) * 2;
                q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
            }
            else if (m11 > m22)
            {
                float s = MathF.Sqrt(1 + m11 - m00 - m22) * 2;
                q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
            }
            else
            {
                float s = MathF.Sqrt(1 + m22 - m00 - m11) * 2;
                q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
            }
            return q.normalized;
        }
        public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.up);

        public Quaternion normalized
        {
            get
            {
                float m = MathF.Sqrt(x * x + y * y + z * z + w * w);
                return m > 1e-8f ? new Quaternion(x / m, y / m, z / m, w / m) : identity;
            }
        }

        public static Quaternion Inverse(Quaternion q) => new Quaternion(-q.x, -q.y, -q.z, q.w);

        public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
        {
            t = Mathf.Clamp01(t);
            float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (dot < 0) { b = new Quaternion(-b.x, -b.y, -b.z, -b.w); dot = -dot; }
            if (dot > 0.9995f)
                return new Quaternion(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t).normalized;
            float theta = MathF.Acos(dot);
            float sa = MathF.Sin((1 - t) * theta) / MathF.Sin(theta), sb = MathF.Sin(t * theta) / MathF.Sin(theta);
            return new Quaternion(a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb, a.w * sa + b.w * sb);
        }

        /// <summary>Euler angles in degrees (Z, X, Y application order).</summary>
        public Vector3 eulerAngles
        {
            get
            {
                // Inverse of Euler(): R = Ry * Rx * Rz.
                float sinX = Mathf.Clamp(2 * (w * x - y * z), -1, 1);
                float ex = MathF.Asin(sinX);
                float ey = MathF.Atan2(2 * (w * y + x * z), 1 - 2 * (x * x + y * y));
                float ez = MathF.Atan2(2 * (w * z + x * y), 1 - 2 * (x * x + z * z));
                return new Vector3(ex, ey, ez) * Mathf.Rad2Deg;
            }
        }

        public static Quaternion operator *(Quaternion a, Quaternion b) => new Quaternion(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);

        public static Vector3 operator *(Quaternion q, Vector3 v)
        {
            Vector3 u = new Vector3(q.x, q.y, q.z);
            Vector3 t = 2f * Vector3.Cross(u, v);
            return v + q.w * t + Vector3.Cross(u, t);
        }

        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "({0:F3}, {1:F3}, {2:F3}, {3:F3})", x, y, z, w);
    }

    public struct Color
    {
        public float r, g, b, a;
        public Color(float r, float g, float b, float a = 1f) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public static Color white => new Color(1, 1, 1);
        public static Color black => new Color(0, 0, 0);
        public static Color red => new Color(1, 0, 0);
        public static Color green => new Color(0, 1, 0);
        public static Color blue => new Color(0, 0, 1);
        public static Color yellow => new Color(1, 0.92f, 0.016f);
        public static Color Lerp(Color a, Color b, float t)
        {
            t = Mathf.Clamp01(t);
            return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
        }
        public static Color HSVToRGB(float h, float s, float v)
        {
            h = (h % 1f + 1f) % 1f * 6f;
            int i = (int)MathF.Floor(h);
            float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
            switch (i)
            {
                case 0: return new Color(v, t, p);
                case 1: return new Color(q, v, p);
                case 2: return new Color(p, v, t);
                case 3: return new Color(p, q, v);
                case 4: return new Color(t, p, v);
                default: return new Color(v, p, q);
            }
        }
        public static Color operator *(Color c, float s) => new Color(c.r * s, c.g * s, c.b * s, c.a * s);
        public override string ToString() => string.Format(CultureInfo.InvariantCulture, "RGBA({0:F3}, {1:F3}, {2:F3}, {3:F3})", r, g, b, a);
    }

    public static class Mathf
    {
        public const float PI = MathF.PI;
        public const float Deg2Rad = MathF.PI / 180f;
        public const float Rad2Deg = 180f / MathF.PI;
        public const float Epsilon = float.Epsilon;
        public const float Infinity = float.PositiveInfinity;

        public static float Sin(float f) => MathF.Sin(f);
        public static float Cos(float f) => MathF.Cos(f);
        public static float Tan(float f) => MathF.Tan(f);
        public static float Atan2(float y, float x) => MathF.Atan2(y, x);
        public static float Sqrt(float f) => MathF.Sqrt(f);
        public static float Abs(float f) => MathF.Abs(f);
        public static int Abs(int i) => Math.Abs(i);
        public static float Min(float a, float b) => MathF.Min(a, b);
        public static float Max(float a, float b) => MathF.Max(a, b);
        public static int Min(int a, int b) => Math.Min(a, b);
        public static int Max(int a, int b) => Math.Max(a, b);
        public static float Pow(float f, float p) => MathF.Pow(f, p);
        public static float Exp(float f) => MathF.Exp(f);
        public static float Floor(float f) => MathF.Floor(f);
        public static float Ceil(float f) => MathF.Ceiling(f);
        public static float Round(float f) => MathF.Round(f);
        public static int FloorToInt(float f) => (int)MathF.Floor(f);
        public static int RoundToInt(float f) => (int)MathF.Round(f);
        public static float Sign(float f) => f >= 0 ? 1f : -1f;
        public static float Clamp(float v, float min, float max) => v < min ? min : v > max ? max : v;
        public static int Clamp(int v, int min, int max) => v < min ? min : v > max ? max : v;
        public static float Clamp01(float v) => Clamp(v, 0, 1);
        public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
        public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;
        public static float InverseLerp(float a, float b, float v) => a != b ? Clamp01((v - a) / (b - a)) : 0f;
        public static float MoveTowards(float current, float target, float maxDelta) =>
            MathF.Abs(target - current) <= maxDelta ? target : current + MathF.Sign(target - current) * maxDelta;
        public static float SmoothStep(float from, float to, float t) { t = Clamp01(t); t = t * t * (3f - 2f * t); return from + (to - from) * t; }
        public static float PingPong(float t, float length) { t = Repeat(t, length * 2f); return length - MathF.Abs(t - length); }
        public static float Repeat(float t, float length) => Clamp(t - MathF.Floor(t / length) * length, 0f, length);
        public static float PerlinNoise(float x, float y)
        {
            // Smooth value noise in [0, 1] (not bit-identical to Unity's Perlin, but similar in use).
            int xi = (int)MathF.Floor(x), yi = (int)MathF.Floor(y);
            float xf = x - xi, yf = y - yi;
            float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
            float a = Hash(xi, yi), b = Hash(xi + 1, yi), c = Hash(xi, yi + 1), d = Hash(xi + 1, yi + 1);
            return Lerp(Lerp(a, b, u), Lerp(c, d, u), v);
        }
        static float Hash(int x, int y)
        {
            uint h = (uint)(x * 374761393 + y * 668265263);
            h = (h ^ (h >> 13)) * 1274126177u;
            return (h ^ (h >> 16)) / (float)uint.MaxValue;
        }
    }

    public static class Random
    {
        static System.Random s_Random = new System.Random();
        public static float value => (float)s_Random.NextDouble();
        public static float Range(float min, float max) => min + (float)s_Random.NextDouble() * (max - min);
        /// <summary>Integer range; max is exclusive like Unity.</summary>
        public static int Range(int min, int max) => s_Random.Next(min, max);
        public static Vector3 insideUnitSphere
        {
            get
            {
                Vector3 v;
                do v = new Vector3(Range(-1f, 1f), Range(-1f, 1f), Range(-1f, 1f)); while (v.sqrMagnitude > 1f);
                return v;
            }
        }
        public static void InitState(int seed) { s_Random = new System.Random(seed); }
    }
}
