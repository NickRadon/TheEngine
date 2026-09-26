using System;
using System.Collections.Generic;
using TheEngine.Internal;

namespace TheEngine
{
    public enum PrimitiveType { Cube = 1, Sphere = 2, Capsule = 3, Cylinder = 4, Plane = 5, Quad = 6 }

    public static class Time
    {
        /// <summary>Seconds since the last frame (0 while paused).</summary>
        public static float deltaTime { get; internal set; }
        /// <summary>Seconds since entering play mode.</summary>
        public static float time { get; internal set; }
        public static int frameCount { get; internal set; }
        public static float timeScale { get; set; } = 1f;
        /// <summary>Interval of FixedUpdate and physics steps (0.02 s).</summary>
        public static float fixedDeltaTime { get; internal set; } = 0.02f;
    }

    public static unsafe class Debug
    {
        public static void Log(object message) => Write(0, message);
        public static void LogWarning(object message) => Write(1, message);
        public static void LogError(object message) => Write(2, message);

        static void Write(int level, object message)
        {
            string text = message?.ToString() ?? "Null";
            fixed (char* p = text) Native.Api.Log(level, p);
        }
    }

    /// <summary>Base class for everything attached to a GameObject.</summary>
    public abstract class Component
    {
        internal ulong m_EntityId;
        GameObject m_GameObject;

        public GameObject gameObject => m_GameObject ??= new GameObject(m_EntityId);
        public Transform transform => gameObject.transform;
        public string name { get => gameObject.name; set => gameObject.name = value; }

        public T GetComponent<T>() where T : Component => gameObject.GetComponent<T>();
        public override string ToString() => $"{name} ({GetType().Name})";
    }

    /// <summary>Unity-style script base class. Override Awake/Start/Update/LateUpdate/OnDestroy (may be private).</summary>
    public abstract class MonoBehaviour : Component
    {
        public bool enabled { get; set; } = true;

        public static void Destroy(GameObject obj) => GameObject.Destroy(obj);
        public static GameObject Instantiate(PrimitiveType type, Vector3 position) =>
            GameObject.CreatePrimitive(type).WithPosition(position);
        /// <summary>Clones a scene object or spawns a prefab (a GameObject field assigned a .prefab in the Inspector).</summary>
        public static GameObject Instantiate(GameObject original) => GameObject.Instantiate(original);
        public static GameObject Instantiate(GameObject original, Vector3 position, Quaternion rotation) =>
            GameObject.Instantiate(original, position, rotation);
        public static T FindObjectOfType<T>() where T : MonoBehaviour => Internal.ScriptHost.FindInstance<T>();
    }

    public sealed unsafe class GameObject
    {
        internal readonly ulong m_Id;
        internal readonly string m_PrefabPath; // set when this refers to a prefab asset rather than a scene object
        Transform m_Transform;

        internal GameObject(ulong id) { m_Id = id; }
        internal static GameObject FromPrefab(string path) => new GameObject(path, true);
        GameObject(string prefabPath, bool _) { m_PrefabPath = prefabPath; }

        /// <summary>True for a prefab asset reference (instantiate it to get a scene object).</summary>
        public bool isPrefab => m_PrefabPath != null;

        public static GameObject Instantiate(GameObject original) => Spawn(original, null, null);
        public static GameObject Instantiate(GameObject original, Vector3 position, Quaternion rotation) => Spawn(original, position, rotation);

        static GameObject Spawn(GameObject original, Vector3? position, Quaternion? rotation)
        {
            if (ReferenceEquals(original, null)) throw new System.ArgumentNullException(nameof(original), "The object you want to instantiate is null.");
            float* pose = stackalloc float[7];
            Vector3 p = position ?? Vector3.zero;
            Quaternion q = rotation ?? Quaternion.identity;
            pose[0] = p.x; pose[1] = p.y; pose[2] = p.z; pose[3] = q.x; pose[4] = q.y; pose[5] = q.z; pose[6] = q.w;
            int hasPose = position.HasValue ? 1 : 0;
            ulong id;
            if (original.m_PrefabPath != null)
                fixed (char* path = original.m_PrefabPath) id = Native.Api.PrefabInstantiate(path, pose, hasPose);
            else
                id = Native.Api.EntityInstantiate(original.m_Id, pose, hasPose);
            return id != 0 ? new GameObject(id) : null;
        }

        /// <summary>Creates an empty GameObject in the active scene.</summary>
        public GameObject(string name) { fixed (char* p = name) m_Id = Native.Api.EntityCreate(p, 0); }

        public ulong instanceId => m_Id;
        public bool isValid => m_PrefabPath != null || Native.Api.EntityExists(m_Id) != 0;
        public Transform transform => m_Transform ??= new Transform(m_Id);

        public string name
        {
            get => m_PrefabPath != null ? System.IO.Path.GetFileNameWithoutExtension(m_PrefabPath) : Native.GetName(m_Id);
            set { fixed (char* p = value) Native.Api.EntitySetName(m_Id, p); }
        }

        public bool activeSelf => Native.Api.EntityGetActive(m_Id) != 0;
        public void SetActive(bool value) => Native.Api.EntitySetActive(m_Id, value ? 1 : 0);

        public T GetComponent<T>() where T : Component
        {
            if (typeof(T) == typeof(Transform)) return transform as T;
            if (typeof(T) == typeof(MeshRenderer))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.MeshRenderer) != 0 ? new MeshRenderer { m_EntityId = m_Id } as T : null;
            if (typeof(T) == typeof(Light))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.Light) != 0 ? new Light { m_EntityId = m_Id } as T : null;
            if (typeof(T) == typeof(Camera))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.Camera) != 0 ? new Camera { m_EntityId = m_Id } as T : null;
            if (typeof(T) == typeof(Rigidbody))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.Rigidbody) != 0 ? new Rigidbody { m_EntityId = m_Id } as T : null;
            if (typeof(T) == typeof(CharacterController))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.CharacterController) != 0 ? new CharacterController { m_EntityId = m_Id } as T : null;
            if (typeof(T) == typeof(Animator))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.Animator) != 0 ? new Animator { m_EntityId = m_Id } as T : null;
            if (typeof(T) == typeof(Collider))
                return Native.Api.HasComponent(m_Id, (int)EngineComponent.Collider) != 0 ? new Collider { m_EntityId = m_Id } as T : null;
            return Internal.ScriptHost.FindInstance<T>(m_Id);
        }

        /// <summary>Adds a script component at runtime (play mode only).</summary>
        public T AddComponent<T>() where T : Component, new()
        {
            if (typeof(T) == typeof(Rigidbody)) { Native.Api.ComponentAdd(m_Id, (int)EngineComponent.Rigidbody); return GetComponent<T>(); }
            if (typeof(T) == typeof(Collider)) { Native.Api.ComponentAdd(m_Id, (int)EngineComponent.Collider); return GetComponent<T>(); }
            if (!typeof(MonoBehaviour).IsAssignableFrom(typeof(T))) throw new System.ArgumentException($"{typeof(T).Name} can't be added at runtime");
            return (T)(object)Internal.ScriptHost.AddComponent(typeof(T), m_Id);
        }

        public static GameObject Find(string name)
        {
            ulong id;
            fixed (char* p = name) id = Native.Api.EntityFind(p);
            return id != 0 ? new GameObject(id) : null;
        }

        public static GameObject CreatePrimitive(PrimitiveType type)
        {
            string name = type.ToString();
            fixed (char* p = name) return new GameObject(Native.Api.EntityCreate(p, (int)type));
        }

        /// <summary>Destroys the object (and its children) at the end of the frame.</summary>
        public static void Destroy(GameObject obj) { if (obj != null) Native.Api.EntityDestroy(obj.m_Id); }

        internal GameObject WithPosition(Vector3 p) { transform.position = p; return this; }

        public override string ToString() => name;
        public override bool Equals(object obj) => obj is GameObject g && g.m_Id == m_Id && g.m_PrefabPath == m_PrefabPath;
        public override int GetHashCode() => m_PrefabPath?.GetHashCode() ?? m_Id.GetHashCode();
        public static bool operator ==(GameObject a, GameObject b) =>
            ReferenceEquals(a, null) ? ReferenceEquals(b, null) || !b.isValid : ReferenceEquals(b, null) ? !a.isValid : a.m_Id == b.m_Id && a.m_PrefabPath == b.m_PrefabPath;
        public static bool operator !=(GameObject a, GameObject b) => !(a == b);
    }

    public sealed unsafe class Transform : Component
    {
        internal Transform(ulong id) { m_EntityId = id; }

        Vector3 GetV(TransformChannel c) { float* f = stackalloc float[4]; Native.Api.TransformGet(m_EntityId, (int)c, f); return new Vector3(f[0], f[1], f[2]); }
        void SetV(TransformChannel c, Vector3 v) { float* f = stackalloc float[4]; f[0] = v.x; f[1] = v.y; f[2] = v.z; Native.Api.TransformSet(m_EntityId, (int)c, f); }
        Quaternion GetQ(TransformChannel c) { float* f = stackalloc float[4]; Native.Api.TransformGet(m_EntityId, (int)c, f); return new Quaternion(f[0], f[1], f[2], f[3]); }
        void SetQ(TransformChannel c, Quaternion q) { float* f = stackalloc float[4]; f[0] = q.x; f[1] = q.y; f[2] = q.z; f[3] = q.w; Native.Api.TransformSet(m_EntityId, (int)c, f); }

        public Vector3 position { get => GetV(TransformChannel.Position); set => SetV(TransformChannel.Position, value); }
        public Vector3 localPosition { get => GetV(TransformChannel.LocalPosition); set => SetV(TransformChannel.LocalPosition, value); }
        public Quaternion rotation { get => GetQ(TransformChannel.Rotation); set => SetQ(TransformChannel.Rotation, value); }
        public Quaternion localRotation { get => GetQ(TransformChannel.LocalRotation); set => SetQ(TransformChannel.LocalRotation, value); }
        public Vector3 localScale { get => GetV(TransformChannel.LocalScale); set => SetV(TransformChannel.LocalScale, value); }
        public Vector3 eulerAngles { get => rotation.eulerAngles; set => rotation = Quaternion.Euler(value); }
        public Vector3 localEulerAngles { get => localRotation.eulerAngles; set => localRotation = Quaternion.Euler(value); }

        public Vector3 forward => rotation * Vector3.forward;
        public Vector3 right => rotation * Vector3.right;
        public Vector3 up => rotation * Vector3.up;

        public Transform parent
        {
            get { ulong p = Native.Api.EntityGetParent(m_EntityId); return p != 0 ? new GameObject(p).transform : null; }
            set => Native.Api.EntitySetParent(m_EntityId, value != null ? value.m_EntityId : 0);
        }

        /// <summary>Rotates by euler angles (degrees) in local space, like Unity's default Space.Self.</summary>
        public void Rotate(Vector3 eulers) => localRotation = localRotation * Quaternion.Euler(eulers);
        public void Rotate(float x, float y, float z) => Rotate(new Vector3(x, y, z));
        public void Rotate(Vector3 axis, float degrees) => localRotation = localRotation * Quaternion.AngleAxis(degrees, axis);
        /// <summary>Moves in local space (relative to the object's rotation), like Unity's default Space.Self.</summary>
        public void Translate(Vector3 translation) => position = position + rotation * translation;
        public void Translate(float x, float y, float z) => Translate(new Vector3(x, y, z));
        public void LookAt(Vector3 target) { Vector3 d = target - position; if (d.sqrMagnitude > 1e-8f) rotation = Quaternion.LookRotation(d); }
        public void LookAt(Transform target) => LookAt(target.position);
    }

    public sealed unsafe class MeshRenderer : Component
    {
        public Color color
        {
            get { float* f = stackalloc float[4]; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.MeshRendererColor, f); return new Color(f[0], f[1], f[2], f[3]); }
            set { float* f = stackalloc float[4]; f[0] = value.r; f[1] = value.g; f[2] = value.b; f[3] = value.a; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.MeshRendererColor, f); }
        }
        public bool enabled
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.MeshRendererEnabled, &f); return f != 0; }
            set { float f = value ? 1 : 0; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.MeshRendererEnabled, &f); }
        }
    }

    public sealed unsafe class Light : Component
    {
        public Color color
        {
            get { float* f = stackalloc float[4]; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.LightColor, f); return new Color(f[0], f[1], f[2], f[3]); }
            set { float* f = stackalloc float[4]; f[0] = value.r; f[1] = value.g; f[2] = value.b; f[3] = value.a; Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.LightColor, f); }
        }
        public float intensity { get => Get(ComponentProperty.LightIntensity); set => Set(ComponentProperty.LightIntensity, value); }
        public float range { get => Get(ComponentProperty.LightRange); set => Set(ComponentProperty.LightRange, value); }
        public bool enabled { get => Get(ComponentProperty.LightEnabled) != 0; set => Set(ComponentProperty.LightEnabled, value ? 1 : 0); }

        float Get(ComponentProperty p) { float f; Native.Api.ComponentGet(m_EntityId, (int)p, &f); return f; }
        void Set(ComponentProperty p, float v) => Native.Api.ComponentSet(m_EntityId, (int)p, &v);
    }

    public sealed unsafe class Camera : Component
    {
        public float fieldOfView
        {
            get { float f; Native.Api.ComponentGet(m_EntityId, (int)ComponentProperty.CameraFieldOfView, &f); return f; }
            set => Native.Api.ComponentSet(m_EntityId, (int)ComponentProperty.CameraFieldOfView, &value);
        }
        public static Camera main
        {
            get
            {
                GameObject go = GameObject.Find("Main Camera");
                return go?.GetComponent<Camera>();
            }
        }
    }
}
