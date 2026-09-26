using TheEngine.Internal;

namespace TheEngine
{
    /// <summary>How AddForce/AddTorque apply their value (same values as Unity).</summary>
    public enum ForceMode { Force = 0, Impulse = 1, VelocityChange = 2, Acceleration = 5 }

    /// <summary>Simulated by the physics engine (Jolt). Transform changes made by scripts teleport the body.</summary>
    public sealed unsafe class Rigidbody : Component
    {
        Vector3 GetV(RigidbodyProperty p) { float* f = stackalloc float[3]; Native.Api.RigidbodyGet(m_EntityId, (int)p, f); return new Vector3(f[0], f[1], f[2]); }
        void SetV(RigidbodyProperty p, Vector3 v) { float* f = stackalloc float[3]; f[0] = v.x; f[1] = v.y; f[2] = v.z; Native.Api.RigidbodySet(m_EntityId, (int)p, f); }
        float GetF(RigidbodyProperty p) { float f; Native.Api.RigidbodyGet(m_EntityId, (int)p, &f); return f; }
        void SetF(RigidbodyProperty p, float v) => Native.Api.RigidbodySet(m_EntityId, (int)p, &v);

        public Vector3 velocity { get => GetV(RigidbodyProperty.Velocity); set => SetV(RigidbodyProperty.Velocity, value); }
        /// <summary>Radians per second.</summary>
        public Vector3 angularVelocity { get => GetV(RigidbodyProperty.AngularVelocity); set => SetV(RigidbodyProperty.AngularVelocity, value); }
        public float mass { get => GetF(RigidbodyProperty.Mass); set => SetF(RigidbodyProperty.Mass, value); }
        public bool useGravity { get => GetF(RigidbodyProperty.UseGravity) != 0; set => SetF(RigidbodyProperty.UseGravity, value ? 1 : 0); }
        public bool isKinematic { get => GetF(RigidbodyProperty.IsKinematic) != 0; set => SetF(RigidbodyProperty.IsKinematic, value ? 1 : 0); }
        public float drag { get => GetF(RigidbodyProperty.Drag); set => SetF(RigidbodyProperty.Drag, value); }
        public float angularDrag { get => GetF(RigidbodyProperty.AngularDrag); set => SetF(RigidbodyProperty.AngularDrag, value); }
        public Vector3 position { get => transform.position; set => transform.position = value; }
        public Quaternion rotation { get => transform.rotation; set => transform.rotation = value; }

        public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force) => Apply(force, mode, 0);
        public void AddForce(float x, float y, float z, ForceMode mode = ForceMode.Force) => AddForce(new Vector3(x, y, z), mode);
        public void AddRelativeForce(Vector3 force, ForceMode mode = ForceMode.Force) => AddForce(transform.rotation * force, mode);
        public void AddTorque(Vector3 torque, ForceMode mode = ForceMode.Force) => Apply(torque, mode, 1);
        public void MovePosition(Vector3 position) => transform.position = position;
        public void MoveRotation(Quaternion rotation) => transform.rotation = rotation;

        void Apply(Vector3 v, ForceMode mode, int torque)
        {
            float* f = stackalloc float[3];
            f[0] = v.x; f[1] = v.y; f[2] = v.z;
            Native.Api.RigidbodyAddForce(m_EntityId, f, (int)mode, torque);
        }
    }

    /// <summary>Box, sphere, capsule or mesh collider (the shape is chosen in the Inspector).</summary>
    public sealed unsafe class Collider : Component
    {
        public bool isTrigger
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.ColliderIsTrigger, &f); return f != 0; }
            set { float f = value ? 1 : 0; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.ColliderIsTrigger, &f); }
        }
        public bool enabled
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.ColliderEnabled, &f); return f != 0; }
            set { float f = value ? 1 : 0; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.ColliderEnabled, &f); }
        }
        public Rigidbody attachedRigidbody => gameObject.GetComponent<Rigidbody>();
    }

    public struct ContactPoint
    {
        public Vector3 point;
        /// <summary>Points away from the other object, towards this one.</summary>
        public Vector3 normal;
    }

    /// <summary>Passed to OnCollisionEnter/OnCollisionExit.</summary>
    public sealed class Collision
    {
        internal Collision(GameObject other, Collider collider, Vector3 point, Vector3 normal, Vector3 relativeVelocity)
        {
            gameObject = other;
            this.collider = collider;
            this.relativeVelocity = relativeVelocity;
            contacts = new[] { new ContactPoint { point = point, normal = -normal } };
        }

        public GameObject gameObject { get; }
        public Collider collider { get; }
        public Transform transform => gameObject.transform;
        public Rigidbody rigidbody => gameObject.GetComponent<Rigidbody>();
        public Vector3 relativeVelocity { get; }
        public ContactPoint[] contacts { get; }
        public ContactPoint GetContact(int index) => contacts[index];
        public int contactCount => contacts.Length;
    }

    public struct RaycastHit
    {
        public Vector3 point;
        public Vector3 normal;
        public float distance;
        internal ulong m_Entity;
        public Collider collider => m_Entity != 0 ? new Collider { m_EntityId = m_Entity } : null;
        public Transform transform => m_Entity != 0 ? new GameObject(m_Entity).transform : null;
        public Rigidbody rigidbody => m_Entity != 0 ? new GameObject(m_Entity).GetComponent<Rigidbody>() : null;
    }

    public static unsafe class Physics
    {
        public static Vector3 gravity
        {
            get { float* f = stackalloc float[3]; Native.Api.PhysicsGravity(0, f); return new Vector3(f[0], f[1], f[2]); }
            set { float* f = stackalloc float[3]; f[0] = value.x; f[1] = value.y; f[2] = value.z; Native.Api.PhysicsGravity(1, f); }
        }

        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = float.PositiveInfinity) =>
            Raycast(origin, direction, out _, maxDistance);

        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance = float.PositiveInfinity)
        {
            float* ray = stackalloc float[6];
            ray[0] = origin.x; ray[1] = origin.y; ray[2] = origin.z;
            ray[3] = direction.x; ray[4] = direction.y; ray[5] = direction.z;
            float* hit = stackalloc float[7];
            ulong entity = 0;
            hitInfo = default;
            if (Native.Api.PhysicsRaycast(ray, maxDistance, hit, &entity) == 0) return false;
            hitInfo.point = new Vector3(hit[0], hit[1], hit[2]);
            hitInfo.normal = new Vector3(hit[3], hit[4], hit[5]);
            hitInfo.distance = hit[6];
            hitInfo.m_Entity = entity;
            return true;
        }
    }
}
