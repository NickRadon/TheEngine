#include "launcher/Launcher.h"

#include "core/Platform.h"
#include "editor/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <im_anim.h>

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using EditorUI::Icon;

namespace
{
    const ImVec4 kAccent(0.13f, 0.47f, 0.85f, 1.0f);

    std::string TimeAgo(long long seconds)
    {
        const long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const long long d = std::max(0LL, now - seconds);
        char buf[64];
        if (d < 60) return "Just now";
        if (d < 3600) std::snprintf(buf, sizeof(buf), "%lld minute%s ago", d / 60, d / 60 == 1 ? "" : "s");
        else if (d < 86400) std::snprintf(buf, sizeof(buf), "%lld hour%s ago", d / 3600, d / 3600 == 1 ? "" : "s");
        else std::snprintf(buf, sizeof(buf), "%lld day%s ago", d / 86400, d / 86400 == 1 ? "" : "s");
        return buf;
    }

    bool AccentButton(const char* label, ImVec2 size = ImVec2(0, 0))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.38f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    void Heading(const char* text)
    {
        if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), ImGui::GetFontSize() * 1.6f);
        ImGui::TextUnformatted(text);
        if (EditorUI::BoldFont()) ImGui::PopFont();
    }
}

Launcher::Launcher()
{
    std::snprintf(m_NewLocation, sizeof(m_NewLocation), "%s", Project::DefaultProjectsLocation().c_str());
    Refresh();
}

void Launcher::Refresh()
{
    m_Projects = Project::RecentProjects();
}

void Launcher::OpenProject(const std::string& path)
{
    if (!Project::IsProject(path))
    {
        m_Error = "Not a TheEngine project: " + path + "\nA project folder contains Assets/ and ProjectSettings/.";
        return;
    }
    m_Selected = path;
}

void Launcher::Draw(float dt)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##Launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar(2);

    DrawSidebar();
    ImGui::SameLine(0, 0);

    // Page content slides/fades in when switching pages (ImAnim).
    const ImGuiID pageId = ImHashStr("LauncherPage");
    static Page lastPage = m_Page;
    if (lastPage != m_Page)
    {
        iam_tween_float(pageId, ImHashStr("alpha"), 0.0f, 0.0f, iam_ease_preset(iam_ease_linear), iam_policy_cut, 0.0f, 0.0f);
        iam_tween_float(pageId, ImHashStr("offset"), 24.0f, 0.0f, iam_ease_preset(iam_ease_linear), iam_policy_cut, 0.0f, 24.0f);
        lastPage = m_Page;
    }
    const float alpha = iam_tween_float(pageId, ImHashStr("alpha"), 1.0f, 0.25f, iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt, 1.0f);
    const float offset = iam_tween_float(pageId, ImHashStr("offset"), 0.0f, 0.3f, iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 22));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (m_Page)
    {
    case Page::Projects: DrawProjects(dt); break;
    case Page::NewProject: DrawNewProject(dt); break;
    case Page::Installs: DrawInstalls(); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::End();
}

void Launcher::DrawSidebar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 18));
    ImGui::BeginChild("##sidebar", ImVec2(230, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImVec2 p = ImGui::GetCursorScreenPos();
    EditorUI::DrawIcon(ImGui::GetWindowDrawList(), Icon::Cube, ImVec2(p.x + 14, p.y + 14), 26.0f, IM_COL32(230, 230, 230, 255));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 36, p.y + 2));
    if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), ImGui::GetFontSize() * 1.25f);
    ImGui::TextUnformatted("TheEngine Hub");
    if (EditorUI::BoldFont()) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 20));

    auto navItem = [&](const char* label, Icon icon, Page page) {
        const bool selected = m_Page == page || (page == Page::Projects && m_Page == Page::NewProject);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 34.0f;
        ImGui::PushID(label);
        if (ImGui::InvisibleButton("##nav", ImVec2(w, h))) m_Page = page;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImGuiID id = ImGui::GetID(label);
        const float glow = iam_tween_float(id, ImHashStr("hover"), selected ? 1.0f : hovered ? 0.5f : 0.0f, 0.15f,
                                           iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, ImGui::GetIO().DeltaTime, 0.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(255, 255, 255, static_cast<int>(18 * glow)), 5.0f);
        if (selected) dl->AddRectFilled(pos, ImVec2(pos.x + 3, pos.y + h), ImGui::ColorConvertFloat4ToU32(kAccent), 2.0f);
        EditorUI::DrawIcon(dl, icon, ImVec2(pos.x + 20, pos.y + h * 0.5f), 16.0f, IM_COL32(210, 210, 210, 255));
        dl->AddText(ImVec2(pos.x + 38, pos.y + (h - ImGui::GetFontSize()) * 0.5f), IM_COL32(225, 225, 225, 255), label);
    };
    navItem("Projects", Icon::Folder, Page::Projects);
    navItem("Installs", Icon::Gizmos, Page::Installs);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40);
    ImGui::TextDisabled("Version %s", Project::kEngineVersion);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Launcher::DrawProjects(float dt)
{
    Heading("Projects");
    const float buttonsW = 250.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonsW + ImGui::GetCursorPosX() - ImGui::GetCursorPosX());
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonsW - 28);
    if (ImGui::Button("Add project from disk", ImVec2(150, 30)))
    {
        std::string folder = Platform::PickFolder("Select a TheEngine project folder");
        if (!folder.empty())
        {
            if (Project::IsProject(folder))
            {
                Project::AddRecent(folder);
                Refresh();
                m_Error.clear();
            }
            else
            {
                m_Error = "That folder is not a TheEngine project (it needs Assets/ and ProjectSettings/).";
            }
        }
    }
    ImGui::SameLine();
    if (AccentButton("New project", ImVec2(92, 30)))
    {
        m_Page = Page::NewProject;
        m_Error.clear();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::SetNextItemWidth(320);
    ImGui::InputTextWithHint("##search", "Search projects", m_Search, sizeof(m_Search));
    if (!m_Error.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", m_Error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 6));

    // Header row
    const float width = ImGui::GetContentRegionAvail().x;
    const float colModified = width * 0.62f, colVersion = width * 0.82f;
    ImVec2 hp = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(hp.x + 52, hp.y), IM_COL32(150, 150, 150, 255), "NAME");
    dl->AddText(ImVec2(hp.x + colModified, hp.y), IM_COL32(150, 150, 150, 255), "MODIFIED");
    dl->AddText(ImVec2(hp.x + colVersion, hp.y), IM_COL32(150, 150, 150, 255), "EDITOR VERSION");
    ImGui::Dummy(ImVec2(width, ImGui::GetFontSize() + 6));
    dl->AddLine(ImVec2(hp.x, hp.y + ImGui::GetFontSize() + 4), ImVec2(hp.x + width, hp.y + ImGui::GetFontSize() + 4), IM_COL32(60, 60, 60, 255));

    ImGui::BeginChild("##list", ImVec2(0, 0));
    int shown = 0;
    for (size_t i = 0; i < m_Projects.size(); ++i)
    {
        const Project::Info& info = m_Projects[i];
        std::string hay = info.name + " " + info.path;
        if (m_Search[0])
        {
            std::string needle = m_Search;
            auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return s; };
            if (lower(hay).find(lower(needle)) == std::string::npos) continue;
        }
        ++shown;
        ImGui::PushID(static_cast<int>(i));
        const float rowH = 54.0f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemAllowOverlap();
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, rowH));
        const bool hovered = ImGui::IsItemHovered();
        const float glow = iam_tween_float(ImGui::GetItemID(), ImHashStr("hover"), hovered ? 1.0f : 0.0f, 0.12f,
                                           iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt, 0.0f);
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        rdl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + rowH), IM_COL32(255, 255, 255, static_cast<int>(6 + 14 * glow)), 6.0f);

        const ImU32 textCol = info.exists ? IM_COL32(230, 230, 230, 255) : IM_COL32(140, 140, 140, 255);
        EditorUI::DrawIcon(rdl, info.exists ? Icon::Cube : Icon::Warning, ImVec2(pos.x + 26, pos.y + rowH * 0.5f), 22.0f,
                           info.exists ? IM_COL32(200, 200, 200, 255) : IM_COL32(240, 190, 60, 255));
        if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), 0.0f);
        rdl->AddText(ImVec2(pos.x + 52, pos.y + 9), textCol, info.name.c_str());
        if (EditorUI::BoldFont()) ImGui::PopFont();
        std::string sub = info.exists ? info.path : info.path + "  (missing)";
        rdl->AddText(ImVec2(pos.x + 52, pos.y + 29), IM_COL32(140, 140, 140, 255), sub.c_str());
        rdl->AddText(ImVec2(pos.x + colModified, pos.y + 19), IM_COL32(190, 190, 190, 255), TimeAgo(info.lastOpened).c_str());
        rdl->AddText(ImVec2(pos.x + colVersion, pos.y + 19), IM_COL32(190, 190, 190, 255), Project::kEngineVersion);

        // Row menu
        ImGui::SetCursorScreenPos(ImVec2(pos.x + w - 36, pos.y + 12));
        if (ImGui::InvisibleButton("##menu", ImVec2(28, 30))) ImGui::OpenPopup("rowmenu");
        EditorUI::DrawIcon(rdl, Icon::Menu, ImVec2(pos.x + w - 22, pos.y + rowH * 0.5f), 16.0f,
                           ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(160, 160, 160, 255));
        bool menuHovered = ImGui::IsItemHovered();
        if (ImGui::BeginPopup("rowmenu"))
        {
            if (ImGui::MenuItem("Show in Explorer", nullptr, false, info.exists)) Platform::RevealInExplorer(info.path);
            if (ImGui::MenuItem("Remove from list"))
            {
                Project::RemoveRecent(info.path);
                ImGui::EndPopup();
                ImGui::PopID();
                Refresh();
                break;
            }
            ImGui::EndPopup();
        }
        if (clicked && !menuHovered)
        {
            if (info.exists) OpenProject(info.path);
            else m_Error = "Project folder not found: " + info.path;
        }
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rowH + 4));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (shown == 0)
    {
        ImGui::Dummy(ImVec2(0, 40));
        const char* msg = m_Projects.empty() ? "No projects yet. Create one to get started." : "No projects match your search.";
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(msg).x) * 0.5f);
        ImGui::TextDisabled("%s", msg);
    }
    ImGui::EndChild();
}

void Launcher::DrawNewProject(float)
{
    Heading("New project");
    ImGui::TextDisabled("Editor version %s", Project::kEngineVersion);
    ImGui::Dummy(ImVec2(0, 10));

    struct TemplateInfo { const char* name; const char* description; };
    static const TemplateInfo templates[] = {
        { "3D (Sample Scene)", "A lit sample scene with the procedural sky, shadows, a checker material and two C# scripts (Rotator, Bobber)." },
        { "Empty 3D", "Just a Main Camera and a Directional Light." },
    };

    const float listW = std::min(420.0f, ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::BeginChild("##templates", ImVec2(listW, -60));
    for (int i = 0; i < 2; ++i)
    {
        ImGui::PushID(i);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = 86.0f;
        if (ImGui::InvisibleButton("##tpl", ImVec2(w, h))) m_Template = i;
        const bool selected = m_Template == i;
        const float glow = iam_tween_float(ImGui::GetItemID(), ImHashStr("sel"), selected ? 1.0f : ImGui::IsItemHovered() ? 0.4f : 0.0f,
                                           0.15f, iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, ImGui::GetIO().DeltaTime, 0.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(255, 255, 255, static_cast<int>(8 + 12 * glow)), 8.0f);
        if (selected) dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), ImGui::ColorConvertFloat4ToU32(kAccent), 8.0f, 0, 2.0f);
        EditorUI::DrawIcon(dl, i == 0 ? Icon::Sky : Icon::Empty, ImVec2(pos.x + 36, pos.y + h * 0.5f), 34.0f, IM_COL32(210, 210, 210, 255));
        if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), 0.0f);
        dl->AddText(ImVec2(pos.x + 72, pos.y + 14), IM_COL32(235, 235, 235, 255), templates[i].name);
        if (EditorUI::BoldFont()) ImGui::PopFont();
        dl->AddText(nullptr, 0.0f, ImVec2(pos.x + 72, pos.y + 36), IM_COL32(160, 160, 160, 255), templates[i].description, nullptr, w - 84);
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + 8));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 24);

    ImGui::BeginChild("##settings", ImVec2(0, -60));
    ImGui::TextUnformatted("Project settings");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("Project name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", m_NewName, sizeof(m_NewName));
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("Location");
    ImGui::SetNextItemWidth(-40);
    ImGui::InputText("##location", m_NewLocation, sizeof(m_NewLocation));
    ImGui::SameLine();
    if (ImGui::Button("...", ImVec2(32, 0)))
    {
        std::string folder = Platform::PickFolder("Choose where to create the project", m_NewLocation);
        if (!folder.empty()) std::snprintf(m_NewLocation, sizeof(m_NewLocation), "%s", folder.c_str());
    }
    const fs::path target = fs::path(m_NewLocation) / m_NewName;
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("Will be created at:");
    ImGui::TextWrapped("%s", target.string().c_str());
    if (!m_Error.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", m_Error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 28 - 260);
    if (ImGui::Button("Cancel", ImVec2(110, 32)))
    {
        m_Page = Page::Projects;
        m_Error.clear();
    }
    ImGui::SameLine();
    const bool validName = m_NewName[0] != 0 && std::string(m_NewName).find_first_of("\\/:*?\"<>|") == std::string::npos;
    ImGui::BeginDisabled(!validName);
    if (AccentButton("Create project", ImVec2(140, 32)))
    {
        std::string error;
        if (Project::Create(target.string(), m_NewName, m_Template == 0 ? Project::Template::Sample3D : Project::Template::Empty3D, error))
        {
            Project::AddRecent(target.string());
            OpenProject(target.string());
        }
        else
        {
            m_Error = error;
        }
    }
    ImGui::EndDisabled();
}

void Launcher::DrawInstalls()
{
    Heading("Installs");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Text("TheEngine %s", Project::kEngineVersion);
    ImGui::TextDisabled("%s", Platform::ExecutablePath().c_str());
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::TextUnformatted("Scripting");
    const std::string dotnet = Platform::FindOnPath("dotnet");
    if (dotnet.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), ".NET SDK not found. Install the .NET SDK to compile C# scripts.");
    else
        ImGui::TextDisabled(".NET SDK: %s", dotnet.c_str());
}
