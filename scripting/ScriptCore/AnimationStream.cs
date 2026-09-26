using TheEngine.Internal;

namespace TheEngine
{
    /// <summary>A bone transform in local or model space.</summary>
    public struct BonePose
    {
        public Vector3 position;
        public Quaternion rotation;
        public Vector3 scale;

        public BonePose(Vector3 position, Quaternion rotation, Vector3 scale)
        {
            this.position = position;
            this.rotation = rotation;
            this.scale = scale;
        }
    }

    /// <summary>
    /// Writable pose passed to MonoBehaviour.OnAnimatorPose(AnimationStream) after the controller
    /// evaluates and before rig constraints. Valid only during that callback.
    /// </summary>
    public readonly unsafe struct AnimationStream
    {
        readonly ulong m_Entity;
        internal AnimationStream(ulong entity) { m_Entity = entity; }

        bool Access(string bone, bool modelSpace, bool write, ref BonePose pose)
        {
            if (string.IsNullOrEmpty(bone)) return false;
            float* data = stackalloc float[10];
            if (write)
            {
                data[0] = pose.position.x; data[1] = pose.position.y; data[2] = pose.position.z;
                data[3] = pose.rotation.x; data[4] = pose.rotation.y; data[5] = pose.rotation.z; data[6] = pose.rotation.w;
                data[7] = pose.scale.x; data[8] = pose.scale.y; data[9] = pose.scale.z;
            }
            int result;
            fixed (char* name = bone)
                result = Native.Api.AnimatorStreamBone(m_Entity, name, modelSpace ? 1 : 0, write ? 1 : 0, data);
            if (result == 0) return false;
            if (!write)
                pose = new BonePose(new Vector3(data[0], data[1], data[2]),
                                    new Quaternion(data[3], data[4], data[5], data[6]),
                                    new Vector3(data[7], data[8], data[9]));
            return true;
        }

        public bool TryGetLocal(string bone, out BonePose pose) { pose = default; return Access(bone, false, false, ref pose); }
        public bool TryGetModel(string bone, out BonePose pose) { pose = default; return Access(bone, true, false, ref pose); }
        public bool SetLocal(string bone, BonePose pose) => Access(bone, false, true, ref pose);
        public bool SetModel(string bone, BonePose pose) => Access(bone, true, true, ref pose);
    }
}
