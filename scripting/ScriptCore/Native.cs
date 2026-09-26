namespace TheEngine.Internal
{
    /// <summary>
    /// Function table provided by the C++ engine. The field order must match ScriptNativeApi in
    /// src/scripting/ScriptEngine.h exactly.
    /// </summary>
    public unsafe struct NativeApi
    {
        public delegate* unmanaged<int, char*, void> Log;
        public delegate* unmanaged<ulong, int> EntityExists;
        public delegate* unmanaged<ulong, char*, int, int> EntityGetName;
        public delegate* unmanaged<ulong, char*, void> EntitySetName;
        public delegate* unmanaged<ulong, int> EntityGetActive;
        public delegate* unmanaged<ulong, int, void> EntitySetActive;
        public delegate* unmanaged<char*, ulong> EntityFind;
        public delegate* unmanaged<char*, int, ulong> EntityCreate;
        public delegate* unmanaged<ulong, void> EntityDestroy;
        public delegate* unmanaged<ulong, ulong> EntityGetParent;
        public delegate* unmanaged<ulong, ulong, void> EntitySetParent;
        public delegate* unmanaged<ulong, int, float*, void> TransformGet;
        public delegate* unmanaged<ulong, int, float*, void> TransformSet;
        public delegate* unmanaged<ulong, int, int> HasComponent;
        public delegate* unmanaged<ulong, int, float*, int> ComponentGet;
        public delegate* unmanaged<ulong, int, float*, void> ComponentSet;
        public delegate* unmanaged<int, int, int> InputGetKey;
        public delegate* unmanaged<int, int, int> InputGetMouseButton;
        public delegate* unmanaged<float*, void> InputGetMouse;
        public delegate* unmanaged<ulong, int, void> ComponentAdd;
        public delegate* unmanaged<ulong, int, float*, int> RigidbodyGet;
        public delegate* unmanaged<ulong, int, float*, void> RigidbodySet;
        public delegate* unmanaged<ulong, float*, int, int, void> RigidbodyAddForce;
        public delegate* unmanaged<float*, float, float*, ulong*, int> PhysicsRaycast;
        public delegate* unmanaged<int, float*, void> PhysicsGravity;
        public delegate* unmanaged<ulong, float*, int, ulong> EntityInstantiate;
        public delegate* unmanaged<char*, float*, int, ulong> PrefabInstantiate;
        public delegate* unmanaged<ulong, char*, int, float, float> AnimatorParam;
        public delegate* unmanaged<ulong, char*, int, int> AnimatorStateName;
        public delegate* unmanaged<ulong, float*, float, int> CharacterMove;
        public delegate* unmanaged<ulong, int, float*, void> CharacterGet;
        public delegate* unmanaged<ulong, int, float*, void> CharacterSet;
        public delegate* unmanaged<int, int, int> CursorState;
        public delegate* unmanaged<ulong, char*, int, int, float, float> AnimatorLayer;
        public delegate* unmanaged<ulong, float, float, void> AnimatorLook;
        public delegate* unmanaged<ulong, float*, void> AnimatorDelta;
    }

    internal static unsafe class Native
    {
        internal static NativeApi Api;

        internal static string GetName(ulong id)
        {
            char* buffer = stackalloc char[256];
            int length = Api.EntityGetName(id, buffer, 256);
            return new string(buffer, 0, System.Math.Max(0, length));
        }
    }

    // Transform channels for TransformGet/TransformSet.
    internal enum TransformChannel
    {
        LocalPosition = 0, LocalRotation = 1, LocalScale = 2, Position = 3, Rotation = 4,
    }

    // Component properties for ComponentGet/ComponentSet (engine component, property).
    internal enum ComponentProperty
    {
        MeshRendererColor = 0,   // float[4]
        MeshRendererEnabled = 1, // float[1]
        LightColor = 2,          // float[4]
        LightIntensity = 3,      // float[1]
        LightRange = 4,          // float[1]
        LightEnabled = 5,        // float[1]
        CameraFieldOfView = 6,   // float[1]
        ColliderIsTrigger = 7,   // float[1]
        ColliderEnabled = 8,     // float[1]
        AnimatorApplyRootMotion = 9, // float[1]
        AnimatorEnabled = 10,    // float[1]
    }

    internal enum EngineComponent { MeshRenderer = 0, Light = 1, Camera = 2, Rigidbody = 3, Collider = 4, Animator = 5, CharacterController = 6 }

    internal enum RigidbodyProperty { Velocity = 0, AngularVelocity = 1, Mass = 2, UseGravity = 3, IsKinematic = 4, Drag = 5, AngularDrag = 6 }
}
