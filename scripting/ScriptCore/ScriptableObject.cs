using System;
using System.IO;
using System.Text.Json;

namespace TheEngine
{
    /// <summary>Reusable, typed JSON asset stored under the project's Assets directory.</summary>
    public abstract class ScriptableObject
    {
        public string assetPath { get; internal set; }
        public string name => assetPath == null ? GetType().Name : Path.GetFileNameWithoutExtension(assetPath);

        static readonly JsonSerializerOptions Options = new JsonSerializerOptions
        {
            IncludeFields = true,
            PropertyNameCaseInsensitive = true,
            WriteIndented = true
        };

        public static T CreateInstance<T>() where T : ScriptableObject, new() => new T();

        public static T Load<T>(string path) where T : ScriptableObject => (T)Load(typeof(T), path);

        internal static ScriptableObject Load(Type type, string path)
        {
            if (!typeof(ScriptableObject).IsAssignableFrom(type))
                throw new ArgumentException("Asset type must derive from ScriptableObject", nameof(type));
            if (string.IsNullOrWhiteSpace(path)) return null;
            string root = Path.GetFullPath("Assets");
            string full = Path.GetFullPath(path);
            if (!full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("ScriptableObject assets must be inside Assets", nameof(path));
            if (!File.Exists(full)) { Debug.LogWarning($"Missing asset: {path}"); return null; }
            try
            {
                var asset = (ScriptableObject)JsonSerializer.Deserialize(File.ReadAllText(full), type, Options);
                if (asset != null) asset.assetPath = path.Replace('\\', '/');
                return asset;
            }
            catch (Exception e) { Debug.LogError($"Could not load {path}: {e.Message}"); return null; }
        }

        public static void Save(ScriptableObject asset, string path)
        {
            if (asset == null) throw new ArgumentNullException(nameof(asset));
            string root = Path.GetFullPath("Assets");
            string full = Path.GetFullPath(path);
            if (!full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("ScriptableObject assets must be inside Assets", nameof(path));
            Directory.CreateDirectory(Path.GetDirectoryName(full));
            File.WriteAllText(full, JsonSerializer.Serialize(asset, asset.GetType(), Options));
            asset.assetPath = path.Replace('\\', '/');
        }
    }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class CreateAssetMenuAttribute : Attribute
    {
        public string menuName, fileName;
    }

    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Enum | AttributeTargets.Class)]
    public sealed class TooltipAttribute : Attribute
    {
        public TooltipAttribute(string text) { }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class TextAreaAttribute : Attribute
    {
        public TextAreaAttribute(int minLines, int maxLines) { }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class HeaderAttribute : Attribute
    {
        public HeaderAttribute(string text) { }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class MinAttribute : Attribute
    {
        public MinAttribute(float value) { }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class RangeAttribute : Attribute
    {
        public RangeAttribute(float min, float max) { }
    }

    public struct LayerMask
    {
        public int value;
        public static implicit operator int(LayerMask mask) => mask.value;
        public static implicit operator LayerMask(int value) => new LayerMask { value = value };
    }
}
