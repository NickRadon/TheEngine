using System;
using TheEngine.Internal;

namespace TheEngine
{
    [Flags]
    public enum CollisionFlags { None = 0, Sides = 1, Above = 2, Below = 4 }

    /// <summary>Capsule character mover (Unity's CharacterController): collides, climbs steps and slopes. Call Move from Update.</summary>
    public sealed unsafe class CharacterController : Component
    {
        enum Prop { Grounded = 0, Velocity = 1, Height = 2, Radius = 3, Center = 4, SlopeLimit = 5, StepOffset = 6, Enabled = 7 }

        float GetF(Prop p) { float* f = stackalloc float[3]; Native.Api.CharacterGet(m_EntityId, (int)p, f); return f[0]; }
        Vector3 GetV(Prop p) { float* f = stackalloc float[3]; Native.Api.CharacterGet(m_EntityId, (int)p, f); return new Vector3(f[0], f[1], f[2]); }
        void SetF(Prop p, float v) { float* f = stackalloc float[3]; f[0] = v; Native.Api.CharacterSet(m_EntityId, (int)p, f); }
        void SetV(Prop p, Vector3 v) { float* f = stackalloc float[3]; f[0] = v.x; f[1] = v.y; f[2] = v.z; Native.Api.CharacterSet(m_EntityId, (int)p, f); }

        /// <summary>Moves by a displacement (world space), colliding with the world. Gravity is up to the caller.</summary>
        public CollisionFlags Move(Vector3 motion)
        {
            float* f = stackalloc float[3];
            f[0] = motion.x; f[1] = motion.y; f[2] = motion.z;
            collisionFlags = (CollisionFlags)Native.Api.CharacterMove(m_EntityId, f, Time.deltaTime);
            return collisionFlags;
        }

        /// <summary>Moves at a speed (units/second) with gravity applied, like Unity's SimpleMove. Returns isGrounded.</summary>
        public bool SimpleMove(Vector3 speed)
        {
            m_FallSpeed = isGrounded ? -1f : m_FallSpeed + Physics.gravity.y * Time.deltaTime;
            Move(new Vector3(speed.x, m_FallSpeed, speed.z) * Time.deltaTime);
            return isGrounded;
        }
        float m_FallSpeed;

        public CollisionFlags collisionFlags { get; private set; }
        public bool isGrounded => GetF(Prop.Grounded) != 0f;
        public Vector3 velocity => GetV(Prop.Velocity);
        public float height { get => GetF(Prop.Height); set => SetF(Prop.Height, value); }
        public float radius { get => GetF(Prop.Radius); set => SetF(Prop.Radius, value); }
        public Vector3 center { get => GetV(Prop.Center); set => SetV(Prop.Center, value); }
        public float slopeLimit { get => GetF(Prop.SlopeLimit); set => SetF(Prop.SlopeLimit, value); }
        public float stepOffset { get => GetF(Prop.StepOffset); set => SetF(Prop.StepOffset, value); }
        public bool enabled { get => GetF(Prop.Enabled) != 0f; set => SetF(Prop.Enabled, value ? 1f : 0f); }
    }

    public enum CursorLockMode { None = 0, Locked = 1, Confined = 2 }

    /// <summary>Mouse cursor state in play mode (Locked hides it and keeps mouse deltas coming; Escape releases it).</summary>
    public static unsafe class Cursor
    {
        public static CursorLockMode lockState
        {
            get => (CursorLockMode)Native.Api.CursorState(0, -1);
            set => Native.Api.CursorState(0, (int)value);
        }
        public static bool visible
        {
            get => Native.Api.CursorState(1, -1) != 0;
            set => Native.Api.CursorState(1, value ? 1 : 0);
        }
    }
}
