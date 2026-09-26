using TheEngine.Internal;

namespace TheEngine
{
    /// <summary>Plays an Animator Controller (state machine) on the object's skinned mesh.</summary>
    public sealed unsafe class Animator : Component
    {
        const int Get = 0, Set = 1, ResetTriggerOp = 2;

        /// <summary>Parameter kinds as stored in the controller (see the Animator window's Parameters tab).</summary>
        public enum ParameterType { Float = 0, Integer = 1, Boolean = 2, Trigger = 3 }

        /// <summary>One controller parameter and its live value.</summary>
        public struct Parameter
        {
            public string name;
            public ParameterType type;
            public float value;
        }

        /// <summary>Live state of one layer (index -1 = nothing is playing yet).</summary>
        public readonly struct LayerState
        {
            internal LayerState(float* f)
            {
                current = (int)f[0];
                next = (int)f[1];
                normalizedTime = f[2];
                transitionProgress = f[3];
                inTransition = f[4] != 0f;
                interrupted = f[5] != 0f;
                weight = f[6];
            }
            /// <summary>Index of the playing state, or -1.</summary>
            public readonly int current;
            /// <summary>Index of the transition's destination state, or -1 when no transition runs.</summary>
            public readonly int next;
            /// <summary>Normalized time of the current state (keeps counting over loops).</summary>
            public readonly float normalizedTime;
            /// <summary>0..1 progress of the running transition.</summary>
            public readonly float transitionProgress;
            public readonly bool inTransition;
            /// <summary>True while the running transition was interrupted (the old blend is frozen and still contributing).</summary>
            public readonly bool interrupted;
            /// <summary>Effective layer weight.</summary>
            public readonly float weight;
        }

        float Param(string name, int op, float value)
        {
            fixed (char* p = name) return Native.Api.AnimatorParam(m_EntityId, p, op, value);
        }

        public void SetFloat(string name, float value) => Param(name, Set, value);
        /// <summary>Exponential approach towards <paramref name="value"/> over <paramref name="dampTime"/> seconds
        /// (Unity's damped SetFloat). Call it once per frame with the frame's deltaTime.</summary>
        public void SetFloat(string name, float value, float dampTime, float deltaTime)
        {
            fixed (char* p = name) Native.Api.AnimatorParamDamped(m_EntityId, p, value, dampTime, deltaTime);
        }
        public float GetFloat(string name) => Param(name, Get, 0f);
        public void SetBool(string name, bool value) => Param(name, Set, value ? 1f : 0f);
        public bool GetBool(string name) => Param(name, Get, 0f) != 0f;
        public void SetInteger(string name, int value) => Param(name, Set, value);
        public int GetInteger(string name) => (int)Param(name, Get, 0f);
        public void SetTrigger(string name) => Param(name, Set, 1f);
        public void ResetTrigger(string name) => Param(name, ResetTriggerOp, 0f);

        // ----- Parameters (enumeration) -----
        /// <summary>Number of parameters declared by the controller.</summary>
        public int parameterCount
        {
            get
            {
                char* buffer = stackalloc char[64];
                float value;
                int count = 0;
                for (; count < 128; ++count)
                    if (Native.Api.AnimatorParamInfo(m_EntityId, count, buffer, 64, &value) < 0) break;
                return count;
            }
        }
        /// <summary>Parameter by index. Returns false when the index is out of range.</summary>
        public bool TryGetParameter(int index, out Parameter parameter)
        {
            parameter = default;
            char* buffer = stackalloc char[64];
            float value;
            int type = Native.Api.AnimatorParamInfo(m_EntityId, index, buffer, 64, &value);
            if (type < 0) return false;
            parameter = new Parameter
            {
                name = new string(buffer), // CopyWide null-terminates
                type = (ParameterType)type,
                value = value,
            };
            return true;
        }

        // ----- State queries -----
        /// <summary>State of one layer in a single native call.</summary>
        public LayerState GetState(int layer = 0)
        {
            float* f = stackalloc float[7];
            Native.Api.AnimatorStateInfo(m_EntityId, layer, f);
            return new LayerState(f);
        }
        /// <summary>Number of layers of the controller (0 when the Animator is not running).</summary>
        public int layerCount
        {
            get { float* f = stackalloc float[7]; return Native.Api.AnimatorStateInfo(m_EntityId, 0, f); }
        }
        /// <summary>Name of a layer's state: the destination while a transition runs, the playing one otherwise.</summary>
        public string StateName(int layer, bool destination = true)
        {
            char* buffer = stackalloc char[128];
            int length = Native.Api.AnimatorStateNameAt(m_EntityId, layer, destination ? 1 : 0, buffer, 128);
            return new string(buffer, 0, System.Math.Max(0, length));
        }
        /// <summary>Name of the state currently playing (the destination while a transition runs).</summary>
        public string currentStateName => StateName(0, true);
        public bool IsInState(string name, int layer = 0) => StateName(layer, true) == name;
        public float normalizedTime(int layer = 0) => GetState(layer).normalizedTime;
        public bool IsInTransition(int layer = 0) => GetState(layer).inTransition;
        public float transitionProgress(int layer = 0) => GetState(layer).transitionProgress;
        /// <summary>True while the running transition was interrupted by another one.</summary>
        public bool IsTransitionInterrupted(int layer = 0) => GetState(layer).interrupted;

        // ----- Layers -----
        public int GetLayerIndex(string layerName) { fixed (char* p = layerName) return (int)Native.Api.AnimatorLayer(m_EntityId, p, 0, 2, 0f); }
        public float GetLayerWeight(int layerIndex) => Native.Api.AnimatorLayer(m_EntityId, null, layerIndex, 0, 0f);
        public void SetLayerWeight(int layerIndex, float weight) => Native.Api.AnimatorLayer(m_EntityId, null, layerIndex, 1, weight);
        public string GetLayerName(int layerIndex)
        {
            char* buffer = stackalloc char[64];
            int length = Native.Api.AnimatorLayerName(m_EntityId, layerIndex, buffer, 64);
            return new string(buffer, 0, System.Math.Max(0, length));
        }

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
        /// <summary>Runtime enable (the Inspector's Animator header checkbox): false freezes the pose.</summary>
        public bool active
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.AnimatorActive, &f); return f != 0; }
            set { float f = value ? 1 : 0; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.AnimatorActive, &f); }
        }
    }
}
