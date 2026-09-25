using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace TheEngine
{
    /// <summary>Makes a private field show up in the Inspector and get saved (like Unity).</summary>
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class SerializeField : Attribute { }

    /// <summary>Hides a public field from the Inspector (like Unity).</summary>
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class HideInInspector : Attribute { }
}

namespace TheEngine.Internal
{
    /// <summary>
    /// Entry points called by the C++ engine (via hostfxr function pointers). User scripts are loaded into a
    /// collectible AssemblyLoadContext so they can be recompiled and reloaded without restarting the editor.
    /// Strings cross the boundary as UTF-16; lists use ASCII separator characters.
    /// </summary>
    public static unsafe class ScriptHost
    {
        const char TypeSep = '\u001D', EntrySep = '\u001E', PartSep = '\u001F';

        sealed class GameLoadContext : AssemblyLoadContext
        {
            public GameLoadContext() : base("TheEngine.Game", isCollectible: true) { }

            protected override Assembly Load(AssemblyName name)
            {
                // Scripts must bind to the already loaded engine API, not a second copy.
                if (name.Name == typeof(MonoBehaviour).Assembly.GetName().Name) return typeof(MonoBehaviour).Assembly;
                return null;
            }
        }

        sealed class Instance
        {
            public MonoBehaviour Behaviour;
            public ulong Entity;
            public Action Awake, Start, Update, LateUpdate, OnDestroy;
            public bool Started;
        }

        static GameLoadContext s_Context;
        static readonly Dictionary<string, Type> s_Types = new Dictionary<string, Type>();
        static readonly Dictionary<int, Instance> s_Instances = new Dictionary<int, Instance>();
        static int s_NextHandle = 1;

        // ------------------------------------------------------------------
        // Lifetime / assembly loading
        // ------------------------------------------------------------------
        [UnmanagedCallersOnly]
        public static int Initialize(NativeApi* api)
        {
            Native.Api = *api;
            return 1;
        }

        [UnmanagedCallersOnly]
        public static int LoadGameAssembly(char* pathPtr)
        {
            try
            {
                ClearInstances();
                s_Types.Clear();
                if (s_Context != null)
                {
                    s_Context.Unload();
                    s_Context = null;
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                }

                string path = new string(pathPtr);
                if (!File.Exists(path)) return 0;

                // Load from memory so the compiler can overwrite the DLL while it is in use.
                s_Context = new GameLoadContext();
                using var dll = new MemoryStream(File.ReadAllBytes(path));
                string pdbPath = Path.ChangeExtension(path, ".pdb");
                using var pdb = File.Exists(pdbPath) ? new MemoryStream(File.ReadAllBytes(pdbPath)) : null;
                Assembly assembly = s_Context.LoadFromStream(dll, pdb);

                foreach (Type t in assembly.GetTypes())
                    if (!t.IsAbstract && typeof(MonoBehaviour).IsAssignableFrom(t))
                        s_Types[t.Name] = t;
                return s_Types.Count;
            }
            catch (Exception e)
            {
                Report("Failed to load scripts", e);
                return -1;
            }
        }

        /// <summary>Script classes and their inspector fields: Class{PartSep}field:type:default...</summary>
        [UnmanagedCallersOnly]
        public static IntPtr GetScriptTypes()
        {
            var sb = new StringBuilder();
            foreach (var (name, type) in s_Types.OrderBy(p => p.Key))
            {
                if (sb.Length > 0) sb.Append(TypeSep);
                sb.Append(name);
                object defaults = null;
                try { defaults = Activator.CreateInstance(type); } catch { }
                foreach (FieldInfo f in InspectorFields(type))
                {
                    string typeName = TypeName(f.FieldType);
                    if (typeName == null) continue;
                    sb.Append(EntrySep).Append(f.Name).Append(PartSep).Append(typeName).Append(PartSep)
                      .Append(defaults != null ? Format(f.GetValue(defaults)) : "");
                }
            }
            return Marshal.StringToHGlobalUni(sb.ToString());
        }

        [UnmanagedCallersOnly]
        public static void FreeString(IntPtr p) => Marshal.FreeHGlobal(p);

        // ------------------------------------------------------------------
        // Instances (play mode)
        // ------------------------------------------------------------------
        [UnmanagedCallersOnly]
        public static int CreateInstance(ulong entity, char* classPtr, char* fieldsPtr, int enabled)
        {
            string className = new string(classPtr);
            if (!s_Types.TryGetValue(className, out Type type))
            {
                Debug.LogWarning($"The referenced script ({className}) on this GameObject is missing!");
                return 0;
            }
            try
            {
                var behaviour = (MonoBehaviour)Activator.CreateInstance(type);
                behaviour.m_EntityId = entity;
                behaviour.enabled = enabled != 0;
                ApplyFields(behaviour, new string(fieldsPtr));
                int handle = Register(behaviour, entity);
                Invoke(s_Instances[handle].Awake, behaviour, "Awake");
                return handle;
            }
            catch (Exception e)
            {
                Report($"Could not create {className}", e);
                return 0;
            }
        }

        internal static T AddComponent<T>(ulong entity) where T : MonoBehaviour, new()
        {
            var behaviour = new T { m_EntityId = entity };
            int handle = Register(behaviour, entity);
            Invoke(s_Instances[handle].Awake, behaviour, "Awake");
            return behaviour;
        }

        [UnmanagedCallersOnly]
        public static void Tick(float deltaTime, float time, int frame)
        {
            Time.deltaTime = deltaTime * Time.timeScale;
            Time.time = time;
            Time.frameCount = frame;

            // Copy: scripts may create or destroy instances while running.
            var instances = s_Instances.Values.ToList();
            foreach (Instance i in instances)
            {
                if (i.Started || !i.Behaviour.enabled || !IsAlive(i)) continue;
                i.Started = true;
                Invoke(i.Start, i.Behaviour, "Start");
            }
            foreach (Instance i in instances)
                if (i.Started && i.Behaviour.enabled && IsAlive(i)) Invoke(i.Update, i.Behaviour, "Update");
            foreach (Instance i in instances)
                if (i.Started && i.Behaviour.enabled && IsAlive(i)) Invoke(i.LateUpdate, i.Behaviour, "LateUpdate");
        }

        [UnmanagedCallersOnly]
        public static void DestroyEntityInstances(ulong entity)
        {
            foreach (var pair in s_Instances.Where(p => p.Value.Entity == entity).ToList())
            {
                Invoke(pair.Value.OnDestroy, pair.Value.Behaviour, "OnDestroy");
                s_Instances.Remove(pair.Key);
            }
        }

        [UnmanagedCallersOnly]
        public static void EndPlay() => ClearInstances();

        [UnmanagedCallersOnly]
        public static IntPtr GetInstanceFields(int handle)
        {
            if (!s_Instances.TryGetValue(handle, out Instance i)) return IntPtr.Zero;
            var sb = new StringBuilder();
            foreach (FieldInfo f in InspectorFields(i.Behaviour.GetType()))
            {
                if (TypeName(f.FieldType) == null) continue;
                if (sb.Length > 0) sb.Append(EntrySep);
                sb.Append(f.Name).Append(PartSep).Append(Format(f.GetValue(i.Behaviour)));
            }
            return Marshal.StringToHGlobalUni(sb.ToString());
        }

        [UnmanagedCallersOnly]
        public static void SetInstanceField(int handle, char* name, char* value)
        {
            if (!s_Instances.TryGetValue(handle, out Instance i)) return;
            ApplyField(i.Behaviour, new string(name), new string(value));
        }

        [UnmanagedCallersOnly]
        public static void SetInstanceEnabled(int handle, int enabled)
        {
            if (s_Instances.TryGetValue(handle, out Instance i)) i.Behaviour.enabled = enabled != 0;
        }

        internal static T FindInstance<T>(ulong entity = 0) where T : class
        {
            foreach (Instance i in s_Instances.Values)
                if ((entity == 0 || i.Entity == entity) && i.Behaviour is T t) return t;
            return null;
        }

        // ------------------------------------------------------------------
        // Helpers
        // ------------------------------------------------------------------
        static int Register(MonoBehaviour behaviour, ulong entity)
        {
            Type type = behaviour.GetType();
            var instance = new Instance
            {
                Behaviour = behaviour,
                Entity = entity,
                Awake = Bind(behaviour, type, "Awake"),
                Start = Bind(behaviour, type, "Start"),
                Update = Bind(behaviour, type, "Update"),
                LateUpdate = Bind(behaviour, type, "LateUpdate"),
                OnDestroy = Bind(behaviour, type, "OnDestroy"),
            };
            int handle = s_NextHandle++;
            s_Instances[handle] = instance;
            return handle;
        }

        static Action Bind(object target, Type type, string method)
        {
            // Unity finds message methods by name regardless of visibility.
            for (Type t = type; t != null && t != typeof(MonoBehaviour); t = t.BaseType)
            {
                MethodInfo m = t.GetMethod(method, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly,
                                           null, Type.EmptyTypes, null);
                if (m != null && m.ReturnType == typeof(void)) return (Action)Delegate.CreateDelegate(typeof(Action), target, m);
            }
            return null;
        }

        static bool IsAlive(Instance i) => Native.Api.EntityExists(i.Entity) != 0;

        static void Invoke(Action action, MonoBehaviour behaviour, string what)
        {
            if (action == null) return;
            try { action(); }
            catch (Exception e) { Report($"{behaviour.GetType().Name}.{what}", e); }
        }

        static void ClearInstances()
        {
            foreach (Instance i in s_Instances.Values.ToList()) Invoke(i.OnDestroy, i.Behaviour, "OnDestroy");
            s_Instances.Clear();
        }

        static void Report(string context, Exception e)
        {
            if (e is TargetInvocationException tie && tie.InnerException != null) e = tie.InnerException;
            Debug.LogError($"{e.GetType().Name}: {e.Message} ({context})\n{e.StackTrace}");
        }

        static IEnumerable<FieldInfo> InspectorFields(Type type) =>
            type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
                .Where(f => !f.IsInitOnly && !f.IsLiteral && f.GetCustomAttribute<HideInInspector>() == null &&
                            (f.IsPublic || f.GetCustomAttribute<SerializeField>() != null));

        static string TypeName(Type t) =>
            t == typeof(float) ? "float" : t == typeof(int) ? "int" : t == typeof(bool) ? "bool" : t == typeof(string) ? "string" :
            t == typeof(Vector3) ? "Vector3" : t == typeof(Color) ? "Color" : null;

        static string Format(object v)
        {
            var c = CultureInfo.InvariantCulture;
            switch (v)
            {
                case float f: return f.ToString("R", c);
                case int i: return i.ToString(c);
                case bool b: return b ? "1" : "0";
                case string s: return s;
                case Vector3 v3: return string.Format(c, "{0:R} {1:R} {2:R}", v3.x, v3.y, v3.z);
                case Color col: return string.Format(c, "{0:R} {1:R} {2:R} {3:R}", col.r, col.g, col.b, col.a);
                default: return "";
            }
        }

        static void ApplyFields(MonoBehaviour behaviour, string fields)
        {
            if (string.IsNullOrEmpty(fields)) return;
            foreach (string entry in fields.Split(EntrySep))
            {
                int sep = entry.IndexOf(PartSep);
                if (sep > 0) ApplyField(behaviour, entry.Substring(0, sep), entry.Substring(sep + 1));
            }
        }

        static void ApplyField(MonoBehaviour behaviour, string name, string value)
        {
            FieldInfo f = InspectorFields(behaviour.GetType()).FirstOrDefault(x => x.Name == name);
            if (f == null) return;
            var c = CultureInfo.InvariantCulture;
            string[] parts = value.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            float P(int i) => i < parts.Length && float.TryParse(parts[i], NumberStyles.Float, c, out float r) ? r : 0f;
            try
            {
                if (f.FieldType == typeof(float)) f.SetValue(behaviour, P(0));
                else if (f.FieldType == typeof(int)) f.SetValue(behaviour, (int)P(0));
                else if (f.FieldType == typeof(bool)) f.SetValue(behaviour, value == "1" || value == "true");
                else if (f.FieldType == typeof(string)) f.SetValue(behaviour, value);
                else if (f.FieldType == typeof(Vector3)) f.SetValue(behaviour, new Vector3(P(0), P(1), P(2)));
                else if (f.FieldType == typeof(Color)) f.SetValue(behaviour, new Color(P(0), P(1), P(2), parts.Length > 3 ? P(3) : 1f));
            }
            catch (Exception e) { Report($"Setting {name}", e); }
        }
    }
}
