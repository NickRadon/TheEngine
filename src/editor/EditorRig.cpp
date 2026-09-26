#include "editor/Editor.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

void Editor::OpenRig(const std::string& path)
{
    std::ifstream in(path);
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && contents.empty()) { Notify("Could not open rig " + fs::path(path).filename().string()); return; }
    if (contents.rfind("TheEngineRig 1", 0) != 0 && contents.rfind("TheEngineRig 2", 0) != 0)
    { Notify("Not a TheEngine rig: " + fs::path(path).filename().string()); return; }
    if (contents.size() >= sizeof(m_RigText)) { Notify("Rig is too large for the editor"); return; }
    std::snprintf(m_RigText, sizeof(m_RigText), "%s", contents.c_str());
    m_RigPath = path;
    m_RigDirty = false;
    m_FocusRig = true;
}

void Editor::SaveRig()
{
    if (m_RigPath.empty()) return;
    std::ofstream out(m_RigPath, std::ios::trunc);
    out << m_RigText;
    if (!out) { Notify("Could not save rig " + fs::path(m_RigPath).filename().string()); return; }
    m_RigDirty = false;
}

void Editor::DrawRigWindow()
{
    if (m_RigPath.empty()) return;
    if (m_FocusRig) { ImGui::SetNextWindowFocus(); m_FocusRig = false; }
    ImGui::SetNextWindowSize(ImVec2(660, 590), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Rig Controls")) { ImGui::End(); return; }
    ImGui::TextUnformatted(fs::path(m_RigPath).filename().string().c_str());
    ImGui::TextDisabled("%s", m_RigPath.c_str());
    if (ImGui::Button("Save")) SaveRig();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) OpenRig(m_RigPath);
    ImGui::SameLine();
    if (m_RigDirty) ImGui::TextColored(ImVec4(1, 0.7f, 0.25f, 1), "Unsaved changes");
    ImGui::SeparatorText("Add control");
    auto append = [this](const char* entry) {
        const std::string current(m_RigText);
        const std::string updated = current + (current.empty() || current.back() == '\n' ? "" : "\n") + entry;
        if (updated.size() < sizeof(m_RigText))
        {
            std::snprintf(m_RigText, sizeof(m_RigText), "%s", updated.c_str());
            m_RigDirty = true;
        }
    };
    if (ImGui::Button("Helper Bone")) append("bone \"helper\" \"root\" 0 0 0 0 0 0 1\n");
    ImGui::SameLine();
    if (ImGui::Button("Copy Bone")) append("copy \"helper\" \"source\" 1 1 1 1\n");
    ImGui::SameLine();
    if (ImGui::Button("Copy Before Look")) append("precopy \"helper\" \"source\" 1 1 1 1\n");
    ImGui::SameLine();
    if (ImGui::Button("Rotate Before Look")) append("prerotate \"bone\" \"space\" 0 0 0 1\n");
    if (ImGui::Button("Modify Bone")) append("modify \"bone\" \"space\" 0 0 0 0 0 0 1 1\n");
    ImGui::SameLine();
    if (ImGui::Button("Two Bone IK")) append("twobone \"hand_l\" \"target\" \"\" 1\n");
    ImGui::TextDisabled("Lines execute in order. Put helper bones before controls; copy targets before IK.");
    ImGui::TextDisabled("Copy flags: translation rotation scale (1/0). Rotate: quaternion xyzw in space bone axes.");
    ImGui::TextDisabled("Modify: position, quaternion xyzw, weight.");
    if (const Skeleton* skeleton = MaskSkeleton())
    {
        if (ImGui::CollapsingHeader("Available bones"))
            for (const std::string& name : skeleton->names) ImGui::TextUnformatted(name.c_str());
    }
    if (ImGui::InputTextMultiline("##rigtext", m_RigText, sizeof(m_RigText), ImVec2(-FLT_MIN, -FLT_MIN))) m_RigDirty = true;
    ImGui::End();
}
