using TheEngine.Internal;

namespace TheEngine
{
    /// <summary>Key codes with the same numeric values as Unity's KeyCode.</summary>
    public enum KeyCode
    {
        None = 0, Backspace = 8, Tab = 9, Return = 13, Escape = 27, Space = 32,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Delete = 127,
        UpArrow = 273, DownArrow = 274, RightArrow = 275, LeftArrow = 276,
        F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        RightShift = 303, LeftShift = 304, RightControl = 305, LeftControl = 306, RightAlt = 307, LeftAlt = 308,
        Mouse0 = 323, Mouse1 = 324, Mouse2 = 325, Mouse3 = 326, Mouse4 = 327,
    }

    /// <summary>Keyboard and mouse input (only delivered while the Game view has focus).</summary>
    public static unsafe class Input
    {
        public static bool GetKey(KeyCode key) => Key(key, 0);
        public static bool GetKeyDown(KeyCode key) => Key(key, 1);
        public static bool GetKeyUp(KeyCode key) => Key(key, 2);
        public static bool GetMouseButton(int button) => Native.Api.InputGetMouseButton(button, 0) != 0;
        public static bool GetMouseButtonDown(int button) => Native.Api.InputGetMouseButton(button, 1) != 0;
        public static bool GetMouseButtonUp(int button) => Native.Api.InputGetMouseButton(button, 2) != 0;

        /// <summary>Mouse position in Game view pixels, origin bottom-left like Unity.</summary>
        public static Vector3 mousePosition { get { float* m = stackalloc float[5]; Native.Api.InputGetMouse(m); return new Vector3(m[0], m[1], 0); } }
        public static Vector2 mouseScrollDelta { get { float* m = stackalloc float[5]; Native.Api.InputGetMouse(m); return new Vector2(0, m[4]); } }

        /// <summary>"Horizontal", "Vertical" (WASD / arrows), "Mouse X", "Mouse Y", "Mouse ScrollWheel".</summary>
        public static float GetAxis(string axis)
        {
            switch (axis)
            {
                case "Horizontal": return (GetKey(KeyCode.D) || GetKey(KeyCode.RightArrow) ? 1f : 0f) - (GetKey(KeyCode.A) || GetKey(KeyCode.LeftArrow) ? 1f : 0f);
                case "Vertical": return (GetKey(KeyCode.W) || GetKey(KeyCode.UpArrow) ? 1f : 0f) - (GetKey(KeyCode.S) || GetKey(KeyCode.DownArrow) ? 1f : 0f);
                case "Mouse X": { float* m = stackalloc float[5]; Native.Api.InputGetMouse(m); return m[2] * 0.1f; }
                case "Mouse Y": { float* m = stackalloc float[5]; Native.Api.InputGetMouse(m); return -m[3] * 0.1f; }
                case "Mouse ScrollWheel": { float* m = stackalloc float[5]; Native.Api.InputGetMouse(m); return m[4] * 0.1f; }
                default: Debug.LogError($"Input axis {axis} is not set up."); return 0f;
            }
        }
        public static float GetAxisRaw(string axis) => GetAxis(axis);

        static bool Key(KeyCode key, int mode) => Native.Api.InputGetKey((int)key, mode) != 0;
    }
}
