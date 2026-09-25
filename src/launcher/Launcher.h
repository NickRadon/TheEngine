#pragma once

#include "core/Project.h"

#include <string>
#include <vector>

// Project launcher shown before the editor (like Unity Hub): list recent projects, create or add projects.
class Launcher
{
public:
    Launcher();
    void Draw(float dt);

    bool HasSelection() const { return !m_Selected.empty(); }
    const std::string& Selected() const { return m_Selected; }

private:
    enum class Page { Projects, NewProject, Installs };

    void DrawSidebar();
    void DrawProjects(float dt);
    void DrawNewProject(float dt);
    void DrawInstalls();
    void Refresh();
    void OpenProject(const std::string& path);

    Page m_Page = Page::Projects;
    std::vector<Project::Info> m_Projects;
    char m_Search[128] = {};
    std::string m_Selected;
    std::string m_Error;

    // New project form
    int m_Template = 0;
    char m_NewName[128] = "My project";
    char m_NewLocation[512] = {};
};
