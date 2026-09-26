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
