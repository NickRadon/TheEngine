using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace TheEngine
{
    /// <summary>Reads Unity Input System .inputactions keyboard/mouse bindings for TheEngine scripts.</summary>
    public sealed class InputActionAsset
    {
        sealed class Binding
        {
            public string Path, Part;
        }

        readonly Dictionary<string, List<Binding>> bindings = new(StringComparer.OrdinalIgnoreCase);

        public static InputActionAsset Load(string path)
        {
            string root = Path.GetFullPath("Assets");
            string full = Path.GetFullPath(path);
            if (!full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("Input actions must be inside Assets", nameof(path));
            using var doc = JsonDocument.Parse(File.ReadAllText(full));
            var result = new InputActionAsset();
            foreach (var map in doc.RootElement.GetProperty("maps").EnumerateArray())
                foreach (var item in map.GetProperty("bindings").EnumerateArray())
                {
                    string action = item.GetProperty("action").GetString();
                    string bindingPath = item.GetProperty("path").GetString();
                    if (string.IsNullOrEmpty(action) || string.IsNullOrEmpty(bindingPath)) continue;
                    if (item.TryGetProperty("isComposite", out var composite) && composite.GetBoolean()) continue;
                    if (!result.bindings.TryGetValue(action, out var list)) result.bindings[action] = list = new List<Binding>();
                    list.Add(new Binding
                    {
                        Path = bindingPath,
                        Part = item.TryGetProperty("isPartOfComposite", out var part) && part.GetBoolean()
                            ? item.GetProperty("name").GetString() : null
                    });
                }
            return result;
        }

        public bool IsPressed(string action) => Match(action, false);
        public bool WasPressedThisFrame(string action) => Match(action, true);

        bool Match(string action, bool down)
        {
            if (!bindings.TryGetValue(action, out var list)) return false;
            foreach (var binding in list)
                if (Read(binding.Path, down)) return true;
            return false;
        }

        public Vector2 ReadVector2(string action)
        {
            if (!bindings.TryGetValue(action, out var list)) return Vector2.zero;
            float x = 0f, y = 0f;
            foreach (var binding in list)
            {
                if (!Read(binding.Path, false)) continue;
                switch (binding.Part?.ToLowerInvariant())
                {
                    case "up": y += 1f; break;
                    case "down": y -= 1f; break;
                    case "left": x -= 1f; break;
                    case "right": x += 1f; break;
                }
            }
            float length = MathF.Sqrt(x * x + y * y);
            return length > 1f ? new Vector2(x / length, y / length) : new Vector2(x, y);
        }

        static bool Read(string path, bool down)
        {
            if (path.StartsWith("<Keyboard>/", StringComparison.OrdinalIgnoreCase))
            {
                string name = path.Substring(11);
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
