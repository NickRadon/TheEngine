using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace TheEngine
{
    /// <summary>Unity Input System .inputactions keyboard/mouse subset: maps, buttons,
    /// 1D/2D composites, mouse delta, and scale/invert processors.</summary>
    public sealed class InputActionAsset
    {
        sealed class Binding
        {
            public string Path, Part, Processors;
            public bool Normalize;
        }

        readonly Dictionary<string, List<Binding>> bindings = new(StringComparer.OrdinalIgnoreCase);

        static string Text(JsonElement element, string name)
        {
            return element.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
                ? value.GetString() ?? "" : "";
        }

        public static InputActionAsset Load(string path)
        {
            string root = Path.GetFullPath("Assets");
            string full = Path.GetFullPath(path);
            if (!full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("Input actions must be inside Assets", nameof(path));
            using var doc = JsonDocument.Parse(File.ReadAllText(full));
            var result = new InputActionAsset();
            foreach (var map in doc.RootElement.GetProperty("maps").EnumerateArray())
            {
                string mapName = Text(map, "name");
                bool normalize = false;
                foreach (var item in map.GetProperty("bindings").EnumerateArray())
                {
                    string action = Text(item, "action");
                    string bindingPath = Text(item, "path");
                    if (action.Length == 0 || bindingPath.Length == 0) continue;
                    if (item.TryGetProperty("isComposite", out var composite) && composite.GetBoolean())
                    {
                        normalize = bindingPath.StartsWith("2DVector", StringComparison.OrdinalIgnoreCase) &&
                                    bindingPath.Contains("mode=1");
                        continue;
                    }
                    string key = mapName + "/" + action;
                    if (!result.bindings.TryGetValue(key, out var list)) result.bindings[key] = list = new List<Binding>();
                    list.Add(new Binding
                    {
                        Path = bindingPath,
                        Part = item.TryGetProperty("isPartOfComposite", out var part) && part.GetBoolean()
                            ? Text(item, "name") : "",
                        Processors = Text(item, "processors"),
                        Normalize = normalize
                    });
                    if (string.IsNullOrEmpty(Text(item, "name")) ||
                        !(item.TryGetProperty("isPartOfComposite", out var isPart) && isPart.GetBoolean())) normalize = false;
                }
            }
            return result;
        }

        List<Binding> Find(string action)
        {
            if (bindings.TryGetValue(action, out var list)) return list;
            List<Binding> found = null;
            foreach (var pair in bindings)
                if (pair.Key.EndsWith("/" + action, StringComparison.OrdinalIgnoreCase))
                {
                    if (found != null) throw new ArgumentException("Ambiguous input action: " + action);
                    found = pair.Value;
                }
            return found;
        }

        public bool IsPressed(string action) => Match(action, false);
        public bool WasPressedThisFrame(string action) => Match(action, true);

        bool Match(string action, bool down)
        {
            var list = Find(action);
            if (list == null) return false;
            foreach (var binding in list)
                if (ReadButton(binding.Path, down)) return true;
            return false;
        }

        public float ReadFloat(string action)
        {
            var list = Find(action);
            if (list == null) return 0f;
            float result = 0f;
            foreach (var binding in list)
            {
                float value = ReadButton(binding.Path, false) ? 1f : 0f;
                if (binding.Part.Equals("negative", StringComparison.OrdinalIgnoreCase)) value = -value;
                result += ApplyScalar(value, binding.Processors);
            }
            return Mathf.Clamp(result, -1f, 1f);
        }

        public Vector2 ReadVector2(string action)
        {
            var list = Find(action);
            if (list == null) return Vector2.zero;
            float digitalX = 0f, digitalY = 0f, analogX = 0f, analogY = 0f;
            bool normalize = false;
            foreach (var binding in list)
            {
                if (binding.Path.Equals("<Mouse>/delta", StringComparison.OrdinalIgnoreCase))
                {
                    float mx = Input.GetAxisRaw("Mouse X"), my = Input.GetAxisRaw("Mouse Y");
                    ApplyVector(ref mx, ref my, binding.Processors);
                    analogX += mx; analogY += my;
                    continue;
                }
                if (!ReadButton(binding.Path, false)) continue;
                switch (binding.Part.ToLowerInvariant())
                {
                    case "up": digitalY += 1f; break;
                    case "down": digitalY -= 1f; break;
                    case "left": digitalX -= 1f; break;
                    case "right": digitalX += 1f; break;
                }
                normalize |= binding.Normalize;
            }
            float length = MathF.Sqrt(digitalX * digitalX + digitalY * digitalY);
            if (normalize && length > 1f) { digitalX /= length; digitalY /= length; }
            return new Vector2(digitalX + analogX, digitalY + analogY);
        }

        static float ProcessorValue(string processors, string name, float fallback)
        {
            var match = Regex.Match(processors ?? "", @"\b" + name + @"\s*=\s*(-?\d+(?:\.\d+)?)",
                                    RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
            return match.Success && float.TryParse(match.Groups[1].Value,
                System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float value)
                ? value : fallback;
        }
        static float ApplyScalar(float value, string processors)
        {
            if ((processors ?? "").IndexOf("invert", StringComparison.OrdinalIgnoreCase) >= 0) value = -value;
            return value * ProcessorValue(processors, "factor", 1f);
        }
        static void ApplyVector(ref float x, ref float y, string processors)
        {
            x *= ProcessorValue(processors, "x", 1f);
            y *= ProcessorValue(processors, "y", 1f);
            if ((processors ?? "").IndexOf("invertVector2", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                if (Regex.IsMatch(processors, @"invertX\s*=\s*true", RegexOptions.IgnoreCase)) x = -x;
                if (Regex.IsMatch(processors, @"invertY\s*=\s*true", RegexOptions.IgnoreCase)) y = -y;
            }
        }

        static bool ReadButton(string path, bool down)
        {
            if (path.StartsWith("<Keyboard>/", StringComparison.OrdinalIgnoreCase))
            {
                string name = path.Substring(11);
                name = name.ToLowerInvariant() switch
                {
                    "leftctrl" => "LeftControl", "rightctrl" => "RightControl",
                    "leftshift" => "LeftShift", "rightshift" => "RightShift",
                    "enter" => "Return", "digit0" => "Alpha0", "digit1" => "Alpha1",
                    _ => name
                };
                return Enum.TryParse<KeyCode>(name, true, out var key) &&
                    (down ? Input.GetKeyDown(key) : Input.GetKey(key));
            }
            if (path.StartsWith("<Mouse>/", StringComparison.OrdinalIgnoreCase))
            {
                int button = path.Substring(8).ToLowerInvariant() switch
                {
                    "leftbutton" => 0, "rightbutton" => 1, "middlebutton" => 2,
                    "backbutton" => 3, "forwardbutton" => 4, _ => -1
                };
                return button >= 0 && (down ? Input.GetMouseButtonDown(button) : Input.GetMouseButton(button));
            }
            return false;
        }
    }
}
