// Dedicated editor for reusable blend mask assets (.mask).
#include "editor/Editor.h"

#include "core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

void Editor::OpenBlendMask(const std::string& path)
{
    BlendMask loaded;
    if (!loaded.Load(path))
    {
        Notify("Could not open blend mask " + fs::path(path).filename().string());
        return;
    }
    m_MaskPath = path;
    m_Mask = std::move(loaded);
    m_FocusMask = true;
}

void Editor::SaveBlendMask()
{
    if (m_MaskPath.empty()) return;
    if (!m_Mask.Save(m_MaskPath))
    {
        Notify("Could not save blend mask " + fs::path(m_MaskPath).filename().string());
        return;
    }
    m_Animation.MaskEdited(m_MaskPath);
}

const Skeleton* Editor::MaskSkeleton() const
{
    if (const EntityId selected = ActiveEntity(); selected != kNullEntity)
        if (const Skeleton* skeleton = m_Animation.SkeletonOf(selected)) return skeleton;
    for (const Entity& e : m_Scene.entities)
        if (e.animator.enabled)
            if (const Skeleton* skeleton = m_Animation.SkeletonOf(e.id)) return skeleton;
    return nullptr;
}

void Editor::DrawBlendMaskWindow()
{
    if (m_MaskPath.empty()) return;
    if (m_FocusMask)
    {
        ImGui::SetNextWindowFocus();
        m_FocusMask = false;
    }
    ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Blend Mask"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(fs::path(m_MaskPath).stem().string().c_str());
    ImGui::TextDisabled("%s", m_MaskPath.c_str());
    ImGui::Text("Bones: %d", static_cast<int>(m_Mask.bones.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Project")) SelectAsset(m_MaskPath);
    ImGui::Separator();

    const Skeleton* skeleton = MaskSkeleton();
    if (skeleton)
    {
        if (ImGui::Button("Select All"))
        {
            m_Mask.bones.clear();
            for (size_t i = 0; i < skeleton->names.size(); ++i)
                if (skeleton->parents[i] < 0) m_Mask.Add(skeleton->names[i]);
            SaveBlendMask();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            m_Mask.bones.clear();
            SaveBlendMask();
        }

        int lowestSpine = -1;
        for (size_t i = 0; i < skeleton->names.size(); ++i)
        {
            std::string lower = skeleton->names[i];
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("spine") != std::string::npos) { lowestSpine = static_cast<int>(i); break; }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(lowestSpine < 0);
        if (ImGui::Button("Upper Body"))
        {
            m_Mask.bones.clear();
            m_Mask.Add(skeleton->names[lowestSpine]);
            SaveBlendMask();
        }
        ImGui::EndDisabled();

        std::vector<std::vector<int>> kids(skeleton->names.size());
        for (size_t i = 0; i < skeleton->parents.size(); ++i)
            if (skeleton->parents[i] >= 0 && skeleton->parents[i] < static_cast<int>(kids.size()))
                kids[skeleton->parents[i]].push_back(static_cast<int>(i));

        ImGui::SeparatorText("Skeleton");
        ImGui::BeginChild("##maskBones", ImVec2(0, 260), ImGuiChildFlags_Borders);
        std::function<void(int)> drawBone = [&](int bone) {
            const std::string& name = skeleton->names[bone];
            std::string ancestor;
            for (int parent = skeleton->parents[bone]; parent >= 0; parent = skeleton->parents[parent])
                if (m_Mask.Contains(skeleton->names[parent])) { ancestor = skeleton->names[parent]; break; }
            bool checked = m_Mask.Contains(name) || !ancestor.empty();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
            if (kids[bone].empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            ImGui::PushID(bone);
            const bool open = ImGui::TreeNodeEx("##bone", flags);
            ImGui::SameLine();
            ImGui::BeginDisabled(!ancestor.empty());
            if (ImGui::Checkbox("##included", &checked))
            {
                if (checked) m_Mask.Add(name);
                else m_Mask.Remove(name);
                SaveBlendMask();
            }
            const bool inheritedHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::EndDisabled();
            if (inheritedHovered && !ancestor.empty()) ImGui::SetTooltip("Included because '%s' is in the mask", ancestor.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            if (open)
            {
                for (const int child : kids[bone]) drawBone(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        for (size_t i = 0; i < skeleton->names.size(); ++i)
            if (skeleton->parents[i] < 0) drawBone(static_cast<int>(i));
        ImGui::EndChild();
    }
    else
    {
        ImGui::TextWrapped("Select an animated object to edit this mask from its skeleton.");
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 34.0f);
    const bool enter = ImGui::InputTextWithHint("##addMaskBone", "Bone name", m_MaskAddBone, sizeof(m_MaskAddBone),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((enter || ImGui::Button("+")) && m_MaskAddBone[0])
    {
        m_Mask.Add(m_MaskAddBone);
        m_MaskAddBone[0] = 0;
        SaveBlendMask();
    }

    if (skeleton)
    {
        std::vector<std::string> missing;
        for (const std::string& bone : m_Mask.bones)
            if (skeleton->Find(bone) < 0) missing.push_back(bone);
        if (!missing.empty())
        {
            ImGui::SeparatorText("Not in this skeleton");
            for (size_t i = 0; i < missing.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i) + 10000);
                ImGui::TextUnformatted(missing[i].c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    m_Mask.Remove(missing[i]);
                    SaveBlendMask();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::TextDisabled("Mask edits save immediately and are not part of controller Undo/Redo.");
    ImGui::End();
}
