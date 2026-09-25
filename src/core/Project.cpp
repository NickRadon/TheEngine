#include "core/Project.h"

#include "core/Log.h"
#include "core/Platform.h"
#include "scene/Material.h"
#include "scene/Scene.h"

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    std::string g_Root;
    std::string g_Name;

    const char* kRotatorScript =
R"(using TheEngine;

// Spins the GameObject. Attached to the Cube in the sample scene.
public class Rotator : MonoBehaviour
{
    public Vector3 degreesPerSecond = new Vector3(0, 45, 0);

    void Update()
    {
        transform.Rotate(degreesPerSecond * Time.deltaTime);
    }
}
)";

    const char* kBobberScript =
R"(using TheEngine;

// Moves the GameObject up and down. Shows Start(), public fields and Mathf.
public class Bobber : MonoBehaviour
{
    public float height = 0.5f;
    public float speed = 2.0f;

    Vector3 start;

    void Start()
    {
        start = transform.position;
    }

    void Update()
    {
        transform.position = start + Vector3.up * (Mathf.Sin(Time.time * speed) * height);
    }
}
)";

    std::string SettingsPath(const std::string& root) { return (fs::path(root) / "ProjectSettings" / "ProjectSettings.txt").string(); }
    std::string RecentPath() { return (fs::path(Platform::AppDataDir()) / "RecentProjects.txt").string(); }

    std::map<std::string, std::string> ReadSettings(const std::string& root)
    {
        std::map<std::string, std::string> values;
        std::ifstream in(SettingsPath(root));
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            std::string key, value;
            if (ls >> key >> std::quoted(value)) values[key] = value;
        }
        return values;
    }

    void WriteSettings(const std::string& root, const std::map<std::string, std::string>& values)
    {
        std::ofstream out(SettingsPath(root));
        for (const auto& [k, v] : values) out << k << ' ' << std::quoted(v) << "\n";
    }

    bool WriteText(const fs::path& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
        return static_cast<bool>(out);
    }

    void WriteCheckerTexture(const fs::path& path)
    {
        const int size = 256;
        std::vector<unsigned char> pixels(size * size * 4);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const bool dark = ((x / 32) + (y / 32)) % 2 == 0;
                unsigned char v = dark ? 150 : 215;
                unsigned char* p = &pixels[(y * size + x) * 4];
                p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
            }
        stbi_write_png(path.string().c_str(), size, size, 4, pixels.data(), size * 4);
    }
}

namespace Project
{
    bool IsProject(const std::string& path)
    {
        std::error_code ec;
        return fs::exists(SettingsPath(path), ec) && fs::is_directory(fs::path(path) / "Assets", ec);
    }

    std::string ReadProjectName(const std::string& path)
    {
        auto settings = ReadSettings(path);
        auto it = settings.find("name");
        return it != settings.end() ? it->second : fs::path(path).filename().string();
    }

    bool Create(const std::string& path, const std::string& name, Template kind, std::string& error)
    {
        std::error_code ec;
        const fs::path root(path);
        if (fs::exists(root, ec) && !fs::is_empty(root, ec))
        {
            error = "The folder already exists and is not empty.";
            return false;
        }
        for (const char* dir : { "Assets/Scenes", "Assets/Scripts", "Assets/Materials", "Assets/Textures", "Assets/Models",
                                 "ProjectSettings", "Library" })
            fs::create_directories(root / dir, ec);
        if (ec)
        {
            error = "Could not create the project folder: " + ec.message();
            return false;
        }

        std::map<std::string, std::string> settings{
            { "name", name },
            { "engineVersion", kEngineVersion },
            { "lastScene", "Assets/Scenes/SampleScene.scene" },
        };
        WriteSettings(root.string(), settings);
        WriteText(root / ".gitignore", "Library/\n");

        Scene scene;
        scene.name = "SampleScene";
        if (kind == Template::Sample3D)
        {
            WriteText(root / "Assets/Scripts/Rotator.cs", kRotatorScript);
            WriteText(root / "Assets/Scripts/Bobber.cs", kBobberScript);
            WriteCheckerTexture(root / "Assets/Textures/Checker.png");
            MaterialAsset checker;
            checker.albedoMap = "Assets/Textures/Checker.png";
            checker.smoothness = 0.25f;
            checker.tiling = glm::vec2(10.0f);
            checker.Save((root / "Assets/Materials/Checker.mat").string());

            scene.CreateDefault();
            for (Entity& e : scene.entities)
                if (e.name == "Ground") e.meshRenderer.material = "Assets/Materials/Checker.mat";
        }
        else
        {
            Entity& cam = scene.Create("Main Camera");
            cam.camera.enabled = true;
            cam.transform.position = { 0.0f, 1.0f, 10.0f };
            Entity& light = scene.Create("Directional Light");
            light.light.enabled = true;
            light.transform.position = { 0.0f, 3.0f, 0.0f };
            light.transform.SetEuler({ -50.0f, -30.0f, 0.0f });
        }
        if (!scene.Save((root / "Assets/Scenes/SampleScene.scene").string()))
        {
            error = "Could not write the sample scene.";
            return false;
        }
        LOG_INFO("Created project '%s' at %s", name.c_str(), path.c_str());
        return true;
    }

    bool Open(const std::string& path, std::string& error)
    {
        if (!IsProject(path))
        {
            error = "Not a TheEngine project (missing ProjectSettings/ProjectSettings.txt): " + path;
            return false;
        }
        std::error_code ec;
        g_Root = fs::absolute(path).lexically_normal().string();
        fs::current_path(g_Root, ec);
        if (ec)
        {
            error = "Cannot enter project folder: " + ec.message();
            return false;
        }
        for (const char* dir : { "Assets", "Library", "ProjectSettings" }) fs::create_directories(dir, ec);
        g_Name = ReadProjectName(g_Root);
        AddRecent(g_Root);
        return true;
    }

    const std::string& Root() { return g_Root; }
    const std::string& Name() { return g_Name; }

    std::string GetSetting(const std::string& key, const std::string& fallback)
    {
        auto settings = ReadSettings(g_Root);
        auto it = settings.find(key);
        return it != settings.end() ? it->second : fallback;
    }

    void SetSetting(const std::string& key, const std::string& value)
    {
        auto settings = ReadSettings(g_Root);
        settings[key] = value;
        WriteSettings(g_Root, settings);
    }

    std::vector<Info> RecentProjects()
    {
        std::vector<Info> list;
        std::ifstream in(RecentPath());
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            Info info;
            if (!(ls >> info.lastOpened >> std::quoted(info.path))) continue;
            info.exists = IsProject(info.path);
            info.name = info.exists ? ReadProjectName(info.path) : fs::path(info.path).filename().string();
            list.push_back(info);
        }
        std::sort(list.begin(), list.end(), [](const Info& a, const Info& b) { return a.lastOpened > b.lastOpened; });
        return list;
    }

    namespace
    {
        void SaveRecent(const std::vector<Info>& list)
        {
            std::ofstream out(RecentPath());
            for (const Info& i : list) out << i.lastOpened << ' ' << std::quoted(i.path) << "\n";
        }
    }

    void AddRecent(const std::string& path)
    {
        const std::string normalized = fs::absolute(path).lexically_normal().string();
        auto list = RecentProjects();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Info& i) { return i.path == normalized; }), list.end());
        Info info;
        info.path = normalized;
        info.lastOpened = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        list.insert(list.begin(), info);
        SaveRecent(list);
    }

    void RemoveRecent(const std::string& path)
    {
        auto list = RecentProjects();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Info& i) { return i.path == path; }), list.end());
        SaveRecent(list);
    }

    std::string DefaultProjectsLocation()
    {
        std::string docs = Platform::DocumentsDir();
        return (fs::path(docs.empty() ? "." : docs) / "TheEngine Projects").string();
    }
}
