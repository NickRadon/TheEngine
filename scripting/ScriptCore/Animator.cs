using TheEngine.Internal;

namespace TheEngine
{
    /// <summary>Plays an Animator Controller (state machine) on the object's skinned mesh.</summary>
    public sealed unsafe class Animator : Component
    {
        const int Get = 0, Set = 1, ResetTriggerOp = 2;

        float Param(string name, int op, float value)
        {
            fixed (char* p = name) return Native.Api.AnimatorParam(m_EntityId, p, op, value);
        }

        public void SetFloat(string name, float value) => Param(name, Set, value);
        public void SetFloat(string name, float value, float dampTime, float deltaTime)
        {
            // Exponential smoothing towards the target, like Unity's damped SetFloat.
            float current = GetFloat(name);
            float t = dampTime > 0f ? 1f - Mathf.Exp(-deltaTime / dampTime) : 1f;
            SetFloat(name, current + (value - current) * t);
        }
        public float GetFloat(string name) => Param(name, Get, 0f);
        public void SetBool(string name, bool value) => Param(name, Set, value ? 1f : 0f);
        public bool GetBool(string name) => Param(name, Get, 0f) != 0f;
        public void SetInteger(string name, int value) => Param(name, Set, value);
        public int GetInteger(string name) => (int)Param(name, Get, 0f);
        public void SetTrigger(string name) => Param(name, Set, 1f);
        public void ResetTrigger(string name) => Param(name, ResetTriggerOp, 0f);

        /// <summary>Name of the state currently playing (the destination while a transition runs).</summary>
        public string currentStateName
        {
            get
            {
                char* buffer = stackalloc char[128];
                int length = Native.Api.AnimatorStateName(m_EntityId, buffer, 128);
                return new string(buffer, 0, System.Math.Max(0, length));
            }
        }
        public bool IsInState(string name) => currentStateName == name;

        // ----- Layers -----
        public int GetLayerIndex(string layerName) { fixed (char* p = layerName) return (int)Native.Api.AnimatorLayer(m_EntityId, p, 0, 2, 0f); }
        public float GetLayerWeight(int layerIndex) => Native.Api.AnimatorLayer(m_EntityId, null, layerIndex, 0, 0f);
        public void SetLayerWeight(int layerIndex, float weight) => Native.Api.AnimatorLayer(m_EntityId, null, layerIndex, 1, weight);

        // ----- Root motion -----
        /// <summary>Root motion of the last animation update, in world space (use it in OnAnimatorMove).</summary>
        public Vector3 deltaPosition { get { float* f = stackalloc float[7]; Native.Api.AnimatorDelta(m_EntityId, f); return new Vector3(f[0], f[1], f[2]); } }
        public Quaternion deltaRotation { get { float* f = stackalloc float[7]; Native.Api.AnimatorDelta(m_EntityId, f); return new Quaternion(f[3], f[4], f[5], f[6]); } }

        /// <summary>Look modifier: turns the spine-to-head chain (the Animator's Look Bones) by pitch (up +) and yaw (left +), degrees.</summary>
        public void SetLookAngles(float pitch, float yaw) => Native.Api.AnimatorLook(m_EntityId, pitch, yaw);

        public bool applyRootMotion
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.AnimatorApplyRootMotion, &f); return f != 0; }
            set { float f = value ? 1 : 0; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.AnimatorApplyRootMotion, &f); }
        }
        public bool enabled
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.AnimatorEnabled, &f); return f != 0; }
            set { float f = value ? 1 : 0; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.AnimatorEnabled, &f); }
        }
    }
}
