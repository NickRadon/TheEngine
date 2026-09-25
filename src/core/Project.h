#pragma once

#include <string>
#include <vector>

// A project is a folder with Assets/ (user content), Library/ (generated caches, safe to delete)
// and ProjectSettings/ProjectSettings.txt, like Unity.
namespace Project
{
    enum class Template { Sample3D, Empty3D };

    struct Info
    {
        std::string name;
        std::string path;
        long long lastOpened = 0; // seconds since epoch
        bool exists = true;
    };

    constexpr const char* kEngineVersion = "0.2";

    bool IsProject(const std::string& path);
    bool Create(const std::string& path, const std::string& name, Template kind, std::string& error);

    // Current project (the working directory is set to its root while the editor runs).
    bool Open(const std::string& path, std::string& error);
    const std::string& Root();
    const std::string& Name();

    // ProjectSettings/ProjectSettings.txt key/value access for the open project.
    std::string GetSetting(const std::string& key, const std::string& fallback = "");
    void SetSetting(const std::string& key, const std::string& value);

    // Recently opened projects (stored in %APPDATA%/TheEngine).
    std::vector<Info> RecentProjects();
    void AddRecent(const std::string& path);
    void RemoveRecent(const std::string& path);

    std::string DefaultProjectsLocation();
    std::string ReadProjectName(const std::string& path);
}
