#include "editor/Editor.h"

#include "core/Log.h"
#include "editor/EditorUI.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using EditorUI::Icon;

namespace
{
    std::string UniquePrefabPath(const std::string& folder, const std::string& name)
    {
        std::string safe = name;
        for (char& c : safe)
            if (std::string("\\/:*?\"<>|").find(c) != std::string::npos) c = '_';
        fs::path path = fs::path(folder) / (safe + ".prefab");
        std::error_code ec;
        for (int i = 1; fs::exists(path, ec); ++i) path = fs::path(folder) / (safe + " " + std::to_string(i) + ".prefab");
        return path.generic_string();
    }

    fs::file_time_type Stamp(const std::string& path)
    {
        std::error_code ec;
        return fs::last_write_time(path, ec);
    }
}

// ---------------------------------------------------------------------------
// Prefab cache: the contents each prefab had when instances were last synced with it. Comparing an instance with
// this version tells which of its properties are overrides.
// ---------------------------------------------------------------------------
const Prefab::Contents* Editor::PrefabContents(const std::string& path)
{
    auto it = m_PrefabCache.find(path);
    if (it != m_PrefabCache.end()) return &it->second;
    Prefab::Contents contents;
    if (!Prefab::Load(path, contents)) return nullptr;
    m_PrefabStamps[path] = Stamp(path);
    return &(m_PrefabCache[path] = std::move(contents));
}

std::vector<EntityId> Editor::PrefabInstanceRoots(const std::string& path) const
{
    std::vector<EntityId> roots;
    for (const Entity& e : m_Scene.entities)
        if (!e.prefab.empty() && (path.empty() || e.prefab == path)) roots.push_back(e.id);
    return roots;
}

void Editor::OnPrefabChanged(const std::string& path, const Prefab::Contents& newContents, EntityId except)
{
    const Prefab::Contents* old = PrefabContents(path);
    const Prefab::Contents previous = old ? *old : newContents;
    int updated = 0;
    for (EntityId root : PrefabInstanceRoots(path))
    {
        if (root == except) continue;
        Prefab::RefreshOverrides(m_Scene, root, previous);
        Prefab::SyncInstance(m_Scene, root, newContents);
        ++updated;
    }
    m_PrefabCache[path] = newContents;
    m_PrefabStamps[path] = Stamp(path);
    if (updated) m_SceneDirty = true;
}

void Editor::SyncPrefabInstancesAfterLoad()
{
    // Stored overrides come from the scene file; everything else follows the prefab as it is now.
    for (EntityId root : PrefabInstanceRoots(""))
    {
        const Entity* e = m_Scene.Find(root);
        if (!e) continue;
        const std::string path = e->prefab;
        if (const Prefab::Contents* contents = PrefabContents(path)) Prefab::SyncInstance(m_Scene, root, *contents);
        else LOG_WARN("Missing prefab %s (used by '%s')", path.c_str(), e->name.c_str());
    }
}

void Editor::RefreshPrefabOverrides()
{
    for (EntityId root : PrefabInstanceRoots(""))
        if (const Prefab::Contents* contents = PrefabContents(m_Scene.Find(root)->prefab))
            Prefab::RefreshOverrides(m_Scene, root, *contents);
}

void Editor::ScanPrefabs()
{
    // Prefab files changed outside the editor (or by another instance of it) update this scene's instances.
    std::vector<std::string> changed;
    for (auto& [path, stamp] : m_PrefabStamps)
    {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        if (Stamp(path) != stamp) changed.push_back(path);
    }
    for (const std::string& path : changed)
    {
        Prefab::Contents contents;
        if (!Prefab::Load(path, contents)) continue;
        LOG_INFO("Prefab %s changed on disk; updating instances", path.c_str());
        if (!m_Playing) PushUndo();
        OnPrefabChanged(path, contents);
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
EntityId Editor::InstantiatePrefab(const std::string& path, EntityId parent, const glm::vec3* position)
{
    const Prefab::Contents* contents = PrefabContents(path);
    if (!contents)
    {
        LOG_ERROR("Could not load prefab %s", path.c_str());
        return kNullEntity;
    }
    PushUndo();
    const EntityId root = Prefab::Instantiate(m_Scene, *contents, path, parent);
    if (position)
    {
        glm::mat4 world = m_Scene.WorldMatrix(root);
        world[3] = glm::vec4(*position, 1.0f);
        m_Scene.SetWorldMatrix(root, world);
    }
    else if (!parent)
    {
        PlaceInFrontOfCamera(root);
    }
    Select(root);
    m_ScrollToEntity = root;
    m_SceneDirty = true;
    return root;
}

std::string Editor::CreatePrefab(EntityId id, const std::string& folder)
{
    const Entity* e = m_Scene.Find(id);
    if (!e) return {};
    if (!m_PrefabModePath.empty())
    {
        Notify("Prefabs can't be created while editing a prefab");
        return {};
    }
    PushUndo();
    std::error_code ec;
    fs::create_directories(folder, ec);
    const std::string path = UniquePrefabPath(folder, e->name);
    if (!Prefab::CreateFromEntity(m_Scene, id, path))
    {
        LOG_ERROR("Failed to write %s", path.c_str());
        return {};
    }
    m_PrefabCache.erase(path);
    PrefabContents(path);
    m_SceneDirty = true;
    Notify("Created prefab " + path);
    return path;
}

void Editor::ApplyPrefabOverrides(EntityId root)
{
    const Entity* e = m_Scene.Find(root);
    if (!e || e->prefab.empty()) return;
    const std::string path = e->prefab;
    PushUndo();
    // Other instances compare against the version before this apply to keep their own overrides.
    const Prefab::Contents* cached = PrefabContents(path);
    const Prefab::Contents previous = cached ? *cached : Prefab::Contents{};
    for (EntityId other : PrefabInstanceRoots(path))
        if (other != root) Prefab::RefreshOverrides(m_Scene, other, previous);
    if (!Prefab::ApplyInstance(m_Scene, root))
    {
        LOG_ERROR("Failed to write %s", path.c_str());
        return;
    }
    Prefab::Contents contents;
    Prefab::Load(path, contents);
    for (EntityId other : PrefabInstanceRoots(path))
        if (other != root) Prefab::SyncInstance(m_Scene, other, contents);
    m_PrefabCache[path] = contents;
    m_PrefabStamps[path] = Stamp(path);
    m_SceneDirty = true;
    Notify("Applied overrides to " + fs::path(path).filename().string());
}

void Editor::RevertPrefabOverrides(EntityId root)
{
    const Entity* e = m_Scene.Find(root);
    if (!e || e->prefab.empty()) return;
    const Prefab::Contents* contents = PrefabContents(e->prefab);
    if (!contents) return;
    PushUndo();
    Prefab::SyncInstance(m_Scene, root, *contents, false);
    m_SceneDirty = true;
    Notify("Reverted " + e->name);
}

void Editor::UnpackPrefab(EntityId root)
{
    PushUndo();
    for (EntityId id : Prefab::InstanceMembers(m_Scene, root))
    {
        Entity* e = m_Scene.Find(id);
        e->prefab.clear();
        e->prefabId = kNullEntity;
        e->prefabOverrides.clear();
    }
    m_SceneDirty = true;
}

// ---------------------------------------------------------------------------
// Prefab mode: edit a prefab asset in isolation (like Unity's Open Prefab).
// ---------------------------------------------------------------------------
void Editor::OpenPrefabMode(const std::string& path)
{
    if (m_Playing) ExitPlayMode();
    if (!m_PrefabModePath.empty()) ClosePrefabMode();
    Prefab::Contents contents;
    if (!Prefab::Load(path, contents))
    {
        LOG_ERROR("Could not load prefab %s", path.c_str());
        return;
    }
    m_PrefabMode.scene = m_Scene;
    m_PrefabMode.selection = m_Selection;
    m_PrefabMode.undo = std::move(m_UndoStack);
    m_PrefabMode.redo = std::move(m_RedoStack);
    m_PrefabMode.sceneDirty = m_SceneDirty;
    m_UndoStack.clear();
    m_RedoStack.clear();

    Scene isolated;
    isolated.name = fs::path(path).stem().string();
    isolated.sky = m_Scene.sky;
    isolated.entities = contents;
    isolated.RecalculateNextId();
    m_Scene = std::move(isolated);
    m_PrefabModePath = path;
    m_SceneDirty = false;
    m_Selection.clear();
    if (!m_Scene.entities.empty()) Select(m_Scene.entities[0].id);
    FrameSelection();
    Notify("Editing prefab " + fs::path(path).filename().string());
}

bool Editor::SavePrefabMode()
{
    if (m_PrefabModePath.empty()) return false;
    // The prefab is the first root and everything under it.
    EntityId root = kNullEntity;
    for (const Entity& e : m_Scene.entities)
        if (e.parent == kNullEntity) { root = e.id; break; }
    if (!root)
    {
        LOG_ERROR("A prefab needs a root object");
        return false;
    }
    Prefab::Contents contents;
    for (const Entity& e : m_Scene.entities)
        if (e.id == root || m_Scene.IsAncestor(root, e.id)) contents.push_back(e);
    std::stable_partition(contents.begin(), contents.end(), [root](const Entity& e) { return e.id == root; });
    if (!Prefab::SaveContents(m_PrefabModePath, contents))
    {
        LOG_ERROR("Failed to save %s", m_PrefabModePath.c_str());
        return false;
    }
    m_PrefabMode.saved = contents;
    m_PrefabMode.hasSaved = true;
    m_SceneDirty = false;
    Notify("Saved " + m_PrefabModePath);
    return true;
}

void Editor::ClosePrefabMode()
{
    if (m_PrefabModePath.empty()) return;
    if (m_SceneDirty) SavePrefabMode(); // auto save, like Unity's default
    const std::string path = m_PrefabModePath;
    m_PrefabModePath.clear();
    m_Scene = std::move(m_PrefabMode.scene);
    m_Selection = m_PrefabMode.selection;
    m_UndoStack = std::move(m_PrefabMode.undo);
    m_RedoStack = std::move(m_PrefabMode.redo);
    m_SceneDirty = m_PrefabMode.sceneDirty;
    if (m_PrefabMode.hasSaved) OnPrefabChanged(path, m_PrefabMode.saved);
    m_PrefabMode = PrefabModeState{};
    m_Selection.erase(std::remove_if(m_Selection.begin(), m_Selection.end(), [this](EntityId id) { return !m_Scene.Find(id); }),
                      m_Selection.end());
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void Editor::DrawPrefabModeBar()
{
    if (m_PrefabModePath.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.27f, 0.40f, 1.0f));
    ImGui::BeginChild("##prefabBar", ImVec2(0, ImGui::GetFrameHeight() + 8), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(4, 4));
    if (ImGui::SmallButton("<")) ClosePrefabMode();
    ImGui::SetItemTooltip("Back to the scene (saves the prefab)");
    if (!m_PrefabModePath.empty())
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Prefab: %s%s", fs::path(m_PrefabModePath).stem().string().c_str(), m_SceneDirty ? "*" : "");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void Editor::DrawPrefabInstanceHeader(EntityId id)
{
    const EntityId root = Prefab::InstanceRoot(m_Scene, id);
    if (!root || m_Playing) return;
    const Entity* r = m_Scene.Find(root);
    const bool missing = !PrefabContents(r->prefab);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::BeginChild("##prefabHeader", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 2 + 6), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(6, 4));
    ImGui::AlignTextToFramePadding();
    if (missing) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Missing prefab: %s", r->prefab.c_str());
    else ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Prefab  %s", fs::path(r->prefab).filename().string().c_str());
    ImGui::SetCursorPosX(6);
    if (!missing)
    {
        if (ImGui::Button("Open")) { OpenPrefabMode(r->prefab); ImGui::EndChild(); ImGui::PopStyleColor(); return; }
        ImGui::SameLine();
        if (ImGui::Button("Select")) SelectAsset(r->prefab);
        ImGui::SameLine();
        if (ImGui::Button("Overrides")) ImGui::OpenPopup("PrefabOverrides");
        if (ImGui::BeginPopup("PrefabOverrides"))
        {
            RefreshPrefabOverrides();
            const std::vector<std::string> overrides = Prefab::DescribeOverrides(m_Scene, root);
            ImGui::TextDisabled("Overrides of %s", r->name.c_str());
            ImGui::Separator();
            if (overrides.empty()) ImGui::TextDisabled("No overrides");
            for (const std::string& o : overrides) ImGui::BulletText("%s", o.c_str());
            ImGui::Separator();
            if (ImGui::Button("Revert All")) { RevertPrefabOverrides(root); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Apply All")) { ApplyPrefabOverrides(root); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Unpack")) UnpackPrefab(root);
    ImGui::SetItemTooltip("Turn the instance into regular objects");
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
