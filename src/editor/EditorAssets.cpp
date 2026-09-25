// Project window (real files under Assets/), asset inspector, materials, models and C# script components.
#include "editor/Editor.h"

#include "core/Log.h"
#include "core/Platform.h"
#include "editor/EditorUI.h"

#include <imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using EditorUI::Icon;

namespace
{
    std::string Ext(const std::string& path)
    {
        std::string e = fs::path(path).extension().string();
        for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e;
    }

    bool IsScene(const std::string& p) { return Ext(p) == ".scene"; }
    bool IsMaterial(const std::string& p) { return Ext(p) == ".mat"; }
    bool IsScript(const std::string& p) { return Ext(p) == ".cs"; }

    std::string Lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // "degreesPerSecond" -> "Degrees Per Second" (Unity's ObjectNames.NicifyVariableName).
    std::string NiceName(const std::string& name)
    {
        std::string out;
        size_t start = (name.size() > 2 && name[0] == 'm' && name[1] == '_') ? 2 : 0;
        for (size_t i = start; i < name.size(); ++i)
        {
            char c = name[i];
            if (c == '_') { out += ' '; continue; }
            if (i > start && std::isupper(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(name[i - 1])))
                out += ' ';
            out += (out.empty() ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        }
        return out;
    }

    std::string UniquePath(const fs::path& desired)
    {
        std::error_code ec;
        if (!fs::exists(desired, ec)) return desired.generic_string();
        const fs::path dir = desired.parent_path();
        const std::string stem = desired.stem().string(), ext = desired.extension().string();
        for (int i = 1;; ++i)
        {
            fs::path candidate = dir / (stem + " " + std::to_string(i) + ext);
            if (!fs::exists(candidate, ec)) return candidate.generic_string();
        }
    }

    Icon AssetIcon(const std::string& path, bool directory)
    {
        if (directory) return Icon::Folder;
        if (IsScene(path)) return Icon::Scene;
        if (ResourceCache::IsModelFile(path)) return Icon::Cube;
        if (IsScript(path)) return Icon::File;
        if (IsMaterial(path)) return Icon::Sky;
        return Icon::File;
    }

    std::vector<std::string> FilesWithExtensions(const std::vector<std::string>& exts)
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator("Assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec) && std::find(exts.begin(), exts.end(), Ext(it->path().string())) != exts.end())
                out.push_back(it->path().generic_string());
        std::sort(out.begin(), out.end());
        return out;
    }
}

// ---------------------------------------------------------------------------
// Drag & drop / selection helpers
// ---------------------------------------------------------------------------
bool Editor::AcceptAssetDrop(const char* extension, std::string& outPath)
{
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    if (!peek || !peek->IsDataType("ASSET")) return false;
    const std::string path(static_cast<const char*>(peek->Data));
    if (Ext(path) != extension) return false;
    if (ImGui::AcceptDragDropPayload("ASSET"))
    {
        outPath = path;
        return true;
    }
    return false;
}

void Editor::SelectAsset(const std::string& path)
{
    m_SelectedAsset = path;
    m_Selection.clear();
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty() && fs::is_directory(parent)) m_ProjectFolder = parent.generic_string();
    ImGui::SetWindowFocus("Inspector");
}

void Editor::AssignMaterial(EntityId id, const std::string& materialPath)
{
    Entity* e = m_Scene.Find(id);
    if (!e || !e->meshRenderer.enabled) return;
    PushUndo();
    e->meshRenderer.material = materialPath;
    Notify("Assigned " + fs::path(materialPath).stem().string() + " to " + e->name);
}

EntityId Editor::InstantiateModel(const std::string& path, EntityId parent, const glm::vec3* position)
{
    const ModelAsset* model = m_Res->GetModel(path);
    if (!model)
    {
        LOG_ERROR("Could not import model %s", path.c_str());
        return kNullEntity;
    }
    PushUndo();
    const EntityId root = m_Scene.Create(UniqueName(m_Scene, fs::path(path).stem().string()), parent).id;
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

    std::vector<EntityId> ids(model->nodes.size(), kNullEntity);
    for (size_t i = 0; i < model->nodes.size(); ++i)
    {
        const ModelNode& node = model->nodes[i];
        const EntityId nodeParent = node.parent < 0 ? root : ids[node.parent];
        const EntityId id = m_Scene.Create(node.name, nodeParent).id;
        ids[i] = id;
        Entity* e = m_Scene.Find(id);
        e->transform.position = node.position;
        e->transform.rotation = node.rotation;
        e->transform.scale = node.scale;
        e->transform.SyncEulerFromRotation();
        auto setMesh = [&](EntityId target, int meshIndex) {
            Entity* t = m_Scene.Find(target);
            t->meshRenderer.enabled = true;
            t->meshRenderer.mesh = path + "#" + std::to_string(meshIndex);
            t->meshRenderer.material = model->meshMaterials[meshIndex];
            t->meshRenderer.color = glm::vec3(1.0f);
        };
        if (node.meshes.size() == 1) setMesh(id, node.meshes[0]);
        else
            for (size_t m = 0; m < node.meshes.size(); ++m)
                setMesh(m_Scene.Create(node.name + " " + std::to_string(m), id).id, node.meshes[m]);
    }
    Select(root);
    m_ScrollToEntity = root;
    Notify("Added " + fs::path(path).filename().string() + " to the scene");
    return root;
}

// ---------------------------------------------------------------------------
// Asset database: script change detection and OS file drops
// ---------------------------------------------------------------------------
void Editor::ScanAssets()
{
    std::map<std::string, fs::file_time_type> stamps;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator("Assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && IsScript(it->path().string()))
            stamps[it->path().generic_string()] = fs::last_write_time(it->path(), ec);
    static bool first = true;
    if (!first && stamps != m_ScriptStamps)
    {
        LOG_INFO("Script changes detected, recompiling");
        m_Scripts->RequestCompile();
    }
    first = false;
    m_ScriptStamps = std::move(stamps);
}

void Editor::OnFilesDropped(const std::vector<std::string>& paths)
{
    m_PendingDrops.insert(m_PendingDrops.end(), paths.begin(), paths.end());
}

void Editor::ProcessDrops()
{
    if (m_PendingDrops.empty()) return;
    std::error_code ec;
    const fs::path root = fs::absolute("Assets");
    int imported = 0;
    for (const std::string& src : m_PendingDrops)
    {
        const fs::path source(src);
        // Files already inside the project are left alone.
        const std::string rel = fs::relative(source, root, ec).generic_string();
        if (!ec && !rel.empty() && rel.rfind("..", 0) != 0) continue;
        const std::string dest = UniquePath(fs::path(m_ProjectFolder) / source.filename());
        if (fs::is_directory(source, ec)) fs::copy(source, dest, fs::copy_options::recursive, ec);
        else fs::copy_file(source, dest, ec);
        if (ec) LOG_ERROR("Could not import %s: %s", src.c_str(), ec.message().c_str());
        else
        {
            ++imported;
            LOG_INFO("Imported %s", dest.c_str());
        }
    }
    m_PendingDrops.clear();
    if (imported) Notify("Imported " + std::to_string(imported) + " asset" + (imported == 1 ? "" : "s"));
    ScanAssets();
}

std::string Editor::CreateAsset(const std::string& folder, const std::string& kind)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::string path;
    if (kind == "folder")
    {
        path = UniquePath(fs::path(folder) / "New Folder");
        fs::create_directories(path, ec);
    }
    else if (kind == "material")
    {
        path = UniquePath(fs::path(folder) / "New Material.mat");
        MaterialAsset().Save(path);
    }
    else if (kind == "script")
    {
        path = UniquePath(fs::path(folder) / "NewBehaviourScript.cs");
        ScriptEngine::WriteScriptTemplate(path, fs::path(path).stem().string());
        // Class names can't contain spaces; UniquePath may have added " 1".
        std::string cls = fs::path(path).stem().string();
        cls.erase(std::remove(cls.begin(), cls.end(), ' '), cls.end());
        const std::string fixed = (fs::path(folder) / (cls + ".cs")).generic_string();
        if (fixed != path)
        {
            fs::rename(path, fixed, ec);
            path = fixed;
            ScriptEngine::WriteScriptTemplate(path, cls);
        }
    }
    else if (kind == "scene")
    {
        path = UniquePath(fs::path(folder) / "New Scene.scene");
        Scene s;
        s.name = fs::path(path).stem().string();
        Entity& cam = s.Create("Main Camera");
        cam.camera.enabled = true;
        cam.transform.position = { 0.0f, 1.0f, 10.0f };
        Entity& light = s.Create("Directional Light");
        light.light.enabled = true;
        light.transform.SetEuler({ -50.0f, -30.0f, 0.0f });
        s.Save(path);
    }
    if (path.empty()) return path;
    m_SelectedAsset = path;
    m_Selection.clear();
    if (kind != "folder")
    {
        // Start renaming right away, like Unity.
        m_RenameAsset = path;
        std::snprintf(m_AssetRenameBuffer, sizeof(m_AssetRenameBuffer), "%s", fs::path(path).stem().string().c_str());
        m_AssetRenameFocus = true;
    }
    if (kind == "script") ScanAssets();
    return path;
}

void Editor::ProjectContextMenu(const std::string& folder)
{
    if (ImGui::BeginMenu("Create"))
    {
        if (ImGui::MenuItem("Folder")) CreateAsset(folder, "folder");
        ImGui::Separator();
        if (ImGui::MenuItem("C# Script")) CreateAsset(folder, "script");
        if (ImGui::MenuItem("Material")) CreateAsset(folder, "material");
        if (ImGui::MenuItem("Scene")) CreateAsset(folder, "scene");
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Import New Asset..."))
    {
        for (const std::string& f : Platform::PickFiles("Import New Asset")) m_PendingDrops.push_back(f);
    }
    if (ImGui::MenuItem("Show in Explorer")) Platform::RevealInExplorer(folder);
}

// ---------------------------------------------------------------------------
// Project window
// ---------------------------------------------------------------------------
void Editor::DrawProject()
{
    if (!ImGui::Begin("Project"))
    {
        ImGui::End();
        return;
    }
    std::error_code ec;
    if (!fs::is_directory(m_ProjectFolder, ec)) m_ProjectFolder = "Assets";

    static float tileSize = 72.0f;
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Toolbar: create menu, search, tile size
    if (EditorUI::IconButton("##projCreate", Icon::Plus, false, "Create", ImVec2(28, ImGui::GetFrameHeight())))
        ImGui::OpenPopup("ProjectCreate");
    if (ImGui::BeginPopup("ProjectCreate"))
    {
        ProjectContextMenu(m_ProjectFolder);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x - 140));
    ImGui::InputTextWithHint("##projSearch", "Search", m_ProjectSearch, sizeof(m_ProjectSearch));
    ImGui::SameLine(ImGui::GetWindowWidth() - 124);
    ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("##tile", &tileSize, 48.0f, 128.0f, "");
    ImGui::Separator();

    // Folder tree (left)
    ImGui::BeginChild("##folders", ImVec2(190, 0), ImGuiChildFlags_Borders);
    std::function<void(const fs::path&)> folderNode = [&](const fs::path& dir) {
        const std::string path = dir.generic_string();
        bool hasChildren = false;
        std::vector<fs::path> children;
        for (auto& entry : fs::directory_iterator(dir, ec))
            if (entry.is_directory(ec)) children.push_back(entry.path());
        std::sort(children.begin(), children.end());
        hasChildren = !children.empty();
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
        if (!hasChildren) f |= ImGuiTreeNodeFlags_Leaf;
        if (path == "Assets") f |= ImGuiTreeNodeFlags_DefaultOpen;
        if (m_ProjectFolder == path) f |= ImGuiTreeNodeFlags_Selected;
        if (m_ProjectFolder.rfind(path + "/", 0) == 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const bool open = ImGui::TreeNodeEx(path.c_str(), f, "      %s", dir.filename().string().c_str());
        ImVec2 mn = ImGui::GetItemRectMin();
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), Icon::Folder,
                           ImVec2(mn.x + ImGui::GetTreeNodeToLabelSpacing() + 7 + ImGui::GetStyle().IndentSpacing * 0.0f,
                                  mn.y + ImGui::GetItemRectSize().y * 0.5f), 14.0f, IM_COL32(194, 194, 194, 255));
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_ProjectFolder = path;
        // Drop assets onto a folder to move them.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET"))
            {
                const fs::path src(static_cast<const char*>(p->Data));
                if (src.parent_path() != dir && path.rfind(src.generic_string(), 0) != 0)
                {
                    const std::string dest = UniquePath(dir / src.filename());
                    fs::rename(src, dest, ec);
                    if (ec) LOG_ERROR("Move failed: %s", ec.message().c_str());
                    else LOG_INFO("Moved %s to %s", src.generic_string().c_str(), dest.c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (open)
        {
            for (const fs::path& c : children) folderNode(c);
            ImGui::TreePop();
        }
    };
    folderNode("Assets");
    ImGui::EndChild();
    ImGui::SameLine();

    // Content (right)
    ImGui::BeginChild("##content", ImVec2(0, 0));
    {
        // Breadcrumbs
        fs::path acc;
        bool first = true;
        for (const auto& part : fs::path(m_ProjectFolder))
        {
            acc /= part;
            if (!first) { ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine(); }
            if (ImGui::SmallButton(part.string().c_str())) m_ProjectFolder = acc.generic_string();
            first = false;
        }
        ImGui::Separator();
    }

    struct Item { std::string path; std::string name; bool dir; };
    std::vector<Item> items;
    if (m_ProjectSearch[0])
    {
        const std::string needle = Lower(m_ProjectSearch);
        for (auto it = fs::recursive_directory_iterator("Assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            if (Lower(it->path().filename().string()).find(needle) != std::string::npos)
                items.push_back({ it->path().generic_string(), it->path().filename().string(), it->is_directory(ec) });
    }
    else
    {
        for (auto& entry : fs::directory_iterator(m_ProjectFolder, ec))
            items.push_back({ entry.path().generic_string(), entry.path().filename().string(), entry.is_directory(ec) });
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.dir != b.dir) return a.dir;
        return Lower(a.name) < Lower(b.name);
    });

    const float cell = tileSize + 16.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    int col = 0;
    std::string openFolder;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& it = items[i];
        ImGui::PushID(it.path.c_str());
        ImGui::BeginGroup();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 tile(cell - 8, tileSize + ImGui::GetTextLineHeight() + 10);
        ImGui::InvisibleButton("##tile", tile);
        const bool hovered = ImGui::IsItemHovered();
        const bool selected = m_SelectedAsset == it.path;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (selected) dl->AddRectFilled(p, ImVec2(p.x + tile.x, p.y + tile.y), IM_COL32(44, 93, 135, 255), 4.0f);
        else if (hovered) dl->AddRectFilled(p, ImVec2(p.x + tile.x, p.y + tile.y), IM_COL32(70, 70, 70, 255), 4.0f);

        const ImVec2 iconCenter(p.x + tile.x * 0.5f, p.y + tileSize * 0.5f + 2);
        const float iconSize = tileSize * 0.72f;
        if (!it.dir && ResourceCache::IsTextureFile(it.path))
        {
            if (ImTextureID thumb = m_Res->Thumbnail(it.path))
                dl->AddImage(thumb, ImVec2(iconCenter.x - iconSize * 0.5f, iconCenter.y - iconSize * 0.5f),
                             ImVec2(iconCenter.x + iconSize * 0.5f, iconCenter.y + iconSize * 0.5f));
        }
        else if (!it.dir && IsMaterial(it.path))
        {
            // Material swatch: a shaded ball in the albedo color.
            const GpuMaterial& mat = m_Res->GetMaterial(it.path);
            const glm::vec3 c = glm::pow(mat.data.albedo, glm::vec3(1.0f / 2.2f));
            const float r = iconSize * 0.42f;
            for (int k = 10; k >= 1; --k)
            {
                const float t = k / 10.0f;
                const float shade = 0.35f + 0.65f * (1.0f - t * 0.8f);
                dl->AddCircleFilled(ImVec2(iconCenter.x - r * 0.25f * (1 - t), iconCenter.y - r * 0.25f * (1 - t)), r * t,
                                    ImGui::ColorConvertFloat4ToU32(ImVec4(c.r * shade, c.g * shade, c.b * shade, 1.0f)), 32);
            }
            if (!mat.data.albedoMap.empty())
                if (ImTextureID thumb = m_Res->Thumbnail(mat.data.albedoMap))
                    dl->AddImageRounded(thumb, ImVec2(iconCenter.x - r * 0.7f, iconCenter.y - r * 0.7f),
                                        ImVec2(iconCenter.x + r * 0.7f, iconCenter.y + r * 0.7f), ImVec2(0, 0), ImVec2(1, 1),
                                        IM_COL32(255, 255, 255, 200), r * 0.7f);
        }
        else
        {
            const ImU32 color = it.dir ? IM_COL32(194, 194, 194, 255)
                              : IsScene(it.path) ? IM_COL32(150, 190, 230, 255)
                              : IsScript(it.path) ? IM_COL32(120, 200, 120, 255) : IM_COL32(170, 170, 170, 255);
            EditorUI::DrawIcon(dl, AssetIcon(it.path, it.dir), iconCenter, iconSize, color);
            if (IsScript(it.path))
                dl->AddText(ImVec2(iconCenter.x - ImGui::CalcTextSize("C#").x * 0.5f, iconCenter.y - 4), IM_COL32(30, 30, 30, 255), "C#");
        }

        // Label or inline rename
        if (m_RenameAsset == it.path)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + tileSize + 2));
            ImGui::SetNextItemWidth(tile.x);
            if (m_AssetRenameFocus)
            {
                ImGui::SetKeyboardFocusHere();
                m_AssetRenameFocus = false;
            }
            const bool commit = ImGui::InputText("##rename", m_AssetRenameBuffer, sizeof(m_AssetRenameBuffer),
                                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (commit || ImGui::IsItemDeactivated())
            {
                const fs::path src(it.path);
                std::string newName = m_AssetRenameBuffer;
                if (IsScript(it.path)) newName.erase(std::remove(newName.begin(), newName.end(), ' '), newName.end());
                const fs::path dst = src.parent_path() / (newName + (it.dir ? "" : src.extension().string()));
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !newName.empty() && dst != src)
                {
                    if (fs::exists(dst, ec)) LOG_ERROR("An asset named %s already exists", dst.generic_string().c_str());
                    else
                    {
                        fs::rename(src, dst, ec);
                        if (ec) LOG_ERROR("Rename failed: %s", ec.message().c_str());
                        else
                        {
                            // Keep C# class names in sync with file names, like Unity's new-script workflow.
                            if (IsScript(it.path))
                            {
                                std::ifstream in(dst);
                                std::stringstream ss;
                                ss << in.rdbuf();
                                in.close();
                                std::string text = ss.str();
                                const std::string oldClass = "class " + src.stem().string();
                                size_t pos = text.find(oldClass);
                                if (pos != std::string::npos)
                                {
                                    text.replace(pos, oldClass.size(), "class " + dst.stem().string());
                                    std::ofstream(dst) << text;
                                }
                            }
                            if (m_SelectedAsset == it.path) m_SelectedAsset = dst.generic_string();
                        }
                    }
                }
                m_RenameAsset.clear();
            }
        }
        else
        {
            const ImVec2 ts = ImGui::CalcTextSize(it.name.c_str());
            dl->PushClipRect(p, ImVec2(p.x + tile.x, p.y + tile.y), true);
            dl->AddText(ImVec2(p.x + std::max(2.0f, (tile.x - ts.x) * 0.5f), p.y + tileSize + 4), IM_COL32(210, 210, 210, 255), it.name.c_str());
            dl->PopClipRect();
        }

        // Interaction
        if (ImGui::IsItemHovered() || hovered)
            ImGui::SetItemTooltip("%s", it.path.c_str());
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_SelectedAsset = it.path;
            m_Selection.clear();
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (it.dir) openFolder = it.path;
            else if (IsScene(it.path)) OpenScene(it.path);
            else if (IsScript(it.path) || ResourceCache::IsTextureFile(it.path)) Platform::OpenWithDefaultApp(it.path);
            else if (ResourceCache::IsModelFile(it.path)) InstantiateModel(it.path, kNullEntity, nullptr);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) m_SelectedAsset = it.path;
        if (ImGui::BeginPopupContextItem("AssetContext"))
        {
            if (ImGui::MenuItem("Open"))
            {
                if (it.dir) openFolder = it.path;
                else if (IsScene(it.path)) OpenScene(it.path);
                else Platform::OpenWithDefaultApp(it.path);
            }
            if (ImGui::MenuItem("Rename", "F2"))
            {
                m_RenameAsset = it.path;
                std::snprintf(m_AssetRenameBuffer, sizeof(m_AssetRenameBuffer), "%s",
                              it.dir ? it.name.c_str() : fs::path(it.path).stem().string().c_str());
                m_AssetRenameFocus = true;
            }
            if (ImGui::MenuItem("Delete", "Del")) m_DeleteAsset = it.path;
            ImGui::Separator();
            if (ImGui::MenuItem("Show in Explorer")) Platform::RevealInExplorer(it.path);
            if (ResourceCache::IsModelFile(it.path) && ImGui::MenuItem("Add to Scene")) InstantiateModel(it.path, kNullEntity, nullptr);
            ImGui::Separator();
            ProjectContextMenu(it.dir ? it.path : m_ProjectFolder);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("ASSET", it.path.c_str(), it.path.size() + 1);
            ImGui::Text("%s", it.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (it.dir && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET"))
            {
                const fs::path src(static_cast<const char*>(pl->Data));
                if (src.generic_string() != it.path)
                {
                    fs::rename(src, UniquePath(fs::path(it.path) / src.filename()), ec);
                    if (ec) LOG_ERROR("Move failed: %s", ec.message().c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndGroup();
        ImGui::PopID();
        if (++col < columns) ImGui::SameLine();
        else col = 0;
    }
    if (items.empty()) ImGui::TextDisabled(m_ProjectSearch[0] ? "No assets match your search." : "This folder is empty.");

    // Empty area: context menu, deselect
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(40.0f, ImGui::GetContentRegionAvail().y)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) m_SelectedAsset.clear();
    if (ImGui::BeginPopupContextWindow("ProjectEmpty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        ProjectContextMenu(m_ProjectFolder);
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    if (!openFolder.empty())
    {
        m_ProjectFolder = openFolder;
        m_ProjectSearch[0] = 0;
    }

    // Keyboard: F2 rename, Delete
    if (windowFocused && !m_SelectedAsset.empty() && !TextInputActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F2))
        {
            m_RenameAsset = m_SelectedAsset;
            const fs::path sp(m_SelectedAsset);
            std::snprintf(m_AssetRenameBuffer, sizeof(m_AssetRenameBuffer), "%s",
                          fs::is_directory(sp, ec) ? sp.filename().string().c_str() : sp.stem().string().c_str());
            m_AssetRenameFocus = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) m_DeleteAsset = m_SelectedAsset;
    }

    // Delete confirmation (files go to the Recycle Bin so they can be restored).
    if (!m_DeleteAsset.empty()) ImGui::OpenPopup("Delete Asset");
    if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete selected asset?");
        ImGui::TextDisabled("%s", m_DeleteAsset.c_str());
        ImGui::TextDisabled("It will be moved to the Recycle Bin.");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(110, 0)))
        {
            if (Platform::MoveToRecycleBin(m_DeleteAsset))
            {
                LOG_INFO("Moved %s to the Recycle Bin", m_DeleteAsset.c_str());
                if (m_SelectedAsset == m_DeleteAsset) m_SelectedAsset.clear();
            }
            else
            {
                LOG_ERROR("Could not delete %s", m_DeleteAsset.c_str());
            }
            m_DeleteAsset.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_DeleteAsset.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Asset inspector
// ---------------------------------------------------------------------------
void Editor::DrawMaterialEditor(const std::string& path)
{
    static std::string loadedPath;
    static fs::file_time_type loadedStamp;
    static MaterialAsset mat;
    std::error_code ec;
    const auto stamp = fs::last_write_time(path, ec);
    if (loadedPath != path || stamp != loadedStamp)
    {
        if (!mat.Load(path))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Could not read %s", path.c_str());
            return;
        }
        loadedPath = path;
        loadedStamp = stamp;
    }

    bool changed = false, texturesChanged = false;
    ImGui::PushID(path.c_str());
    ImGui::TextDisabled("Shader: Standard");

    // Texture slot: thumbnail button with a picker, and drops from the Project window.
    auto textureSlot = [&](const char* label, std::string& texPath) {
        ImGui::PushID(label);
        EditorUI::PropertyLabel(label);
        const float sz = ImGui::GetFrameHeight() * 1.6f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##slot", ImVec2(sz, sz))) ImGui::OpenPopup("TexturePicker");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(40, 40, 40, 255), 3.0f);
        if (ImTextureID thumb = texPath.empty() ? 0 : m_Res->Thumbnail(texPath))
            dl->AddImage(thumb, ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + sz - 2, p.y + sz - 2));
        dl->AddRect(p, ImVec2(p.x + sz, p.y + sz), ImGui::IsItemHovered() ? IM_COL32(58, 121, 187, 255) : IM_COL32(25, 25, 25, 255), 3.0f);
        if (ImGui::BeginDragDropTarget())
        {
            for (const char* ext : { ".png", ".jpg", ".jpeg", ".tga", ".bmp" })
            {
                std::string dropped;
                if (AcceptAssetDrop(ext, dropped))
                {
                    texPath = dropped;
                    changed = texturesChanged = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", texPath.empty() ? "None" : fs::path(texPath).filename().string().c_str());
        if (ImGui::BeginPopup("TexturePicker"))
        {
            if (ImGui::Selectable("None", texPath.empty())) { texPath.clear(); changed = texturesChanged = true; }
            for (const std::string& t : FilesWithExtensions({ ".png", ".jpg", ".jpeg", ".tga", ".bmp" }))
                if (ImGui::Selectable(t.c_str(), t == texPath)) { texPath = t; changed = texturesChanged = true; }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };

    textureSlot("Albedo Map", mat.albedoMap);
    EditorUI::PropertyLabel("Albedo");
    changed |= ImGui::ColorEdit3("##albedo", glm::value_ptr(mat.albedo));
    EditorUI::PropertyLabel("Metallic");
    changed |= ImGui::SliderFloat("##metallic", &mat.metallic, 0.0f, 1.0f);
    EditorUI::PropertyLabel("Smoothness");
    changed |= ImGui::SliderFloat("##smoothness", &mat.smoothness, 0.0f, 1.0f);
    textureSlot("Normal Map", mat.normalMap);
    if (!mat.normalMap.empty())
    {
        EditorUI::PropertyLabel("Normal Strength");
        changed |= ImGui::SliderFloat("##normalStrength", &mat.normalStrength, 0.0f, 2.0f);
    }
    textureSlot("Mask Map (G rough, B metal)", mat.maskMap);
    EditorUI::PropertyLabel("Tiling");
    changed |= ImGui::DragFloat2("##tiling", glm::value_ptr(mat.tiling), 0.05f, 0.0f, 1000.0f);
    EditorUI::PropertyLabel("Emission");
    changed |= ImGui::ColorEdit3("##emission", glm::value_ptr(mat.emission), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::PopID();

    if (changed)
    {
        mat.Save(path);
        loadedStamp = fs::last_write_time(path, ec);
        if (texturesChanged) m_Res->Invalidate(path);
        else m_Res->UpdateMaterialData(path, mat);
    }
}

void Editor::DrawScriptComponent(Entity& e, size_t index, bool& removed)
{
    ScriptComponent& sc = e.scripts[index];
    const ScriptClassInfo* info = m_Scripts->FindClass(sc.className);
    const std::string title = sc.className + " (Script)";
    bool enabled = sc.enabled;
    const bool open = EditorUI::ComponentHeader(title.c_str(), Icon::File, &enabled, &removed);
    if (enabled != sc.enabled)
    {
        MarkEdited();
        sc.enabled = enabled;
        m_Scripts->SetInstanceEnabled(m_Scripts->InstanceHandle(e.id, index), enabled);
    }
    if (!open) return;

    // Script asset reference (click to select the .cs file).
    EditorUI::PropertyLabel("Script");
    const std::string file = sc.className + ".cs";
    if (ImGui::Button(file.c_str(), ImVec2(-FLT_MIN, 0)))
        for (const std::string& f : FilesWithExtensions({ ".cs" }))
            if (fs::path(f).stem().string() == sc.className) SelectAsset(f);

    if (!info)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        if (m_Scripts->IsCompiling()) ImGui::TextWrapped("Compiling scripts...");
        else if (!m_Scripts->Available()) ImGui::TextWrapped("C# scripting is unavailable: %s", m_Scripts->Status().c_str());
        else ImGui::TextWrapped("The associated script can not be loaded. Please fix any compile errors and assign a valid script.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        return;
    }

    // Keep stored fields in sync with the compiled class (new fields get their C# default).
    for (const ScriptFieldInfo& f : info->fields)
    {
        ScriptField* stored = sc.FindField(f.name);
        if (!stored) sc.fields.push_back({ f.name, f.type, f.defaultValue });
        else if (stored->type != f.type) { stored->type = f.type; stored->value = f.defaultValue; }
    }

    const int handle = m_Scripts->InstanceHandle(e.id, index);
    const auto live = handle ? m_Scripts->InstanceFields(handle) : std::map<std::string, std::string>{};
    for (const ScriptFieldInfo& f : info->fields)
    {
        ScriptField* stored = sc.FindField(f.name);
        std::string value = stored->value;
        if (handle)
        {
            auto it = live.find(f.name);
            if (it != live.end()) value = it->second;
        }
        const std::string label = NiceName(f.name);
        ImGui::PushID(f.name.c_str());
        bool edited = false;
        std::string newValue = value;
        std::istringstream in(value);
        std::ostringstream out;
        if (f.type == "float")
        {
            float v = 0.0f;
            in >> v;
            EditorUI::PropertyLabel(label.c_str());
            if (ImGui::DragFloat("##v", &v, 0.05f)) { out << v; edited = true; }
        }
        else if (f.type == "int")
        {
            int v = 0;
            in >> v;
            EditorUI::PropertyLabel(label.c_str());
            if (ImGui::DragInt("##v", &v)) { out << v; edited = true; }
        }
        else if (f.type == "bool")
        {
            bool v = value == "1" || value == "true";
            EditorUI::PropertyLabel(label.c_str());
            if (ImGui::Checkbox("##v", &v)) { out << (v ? "1" : "0"); edited = true; }
        }
        else if (f.type == "string")
        {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
            EditorUI::PropertyLabel(label.c_str());
            if (ImGui::InputText("##v", buffer, sizeof(buffer))) { out << buffer; edited = true; }
        }
        else if (f.type == "Vector3")
        {
            glm::vec3 v(0.0f);
            in >> v.x >> v.y >> v.z;
            if (EditorUI::Vec3Field(label.c_str(), glm::value_ptr(v), 0.05f, 0.0f)) { out << v.x << ' ' << v.y << ' ' << v.z; edited = true; }
        }
        else if (f.type == "Color")
        {
            glm::vec4 v(1.0f);
            in >> v.r >> v.g >> v.b >> v.a;
            EditorUI::PropertyLabel(label.c_str());
            if (ImGui::ColorEdit4("##v", glm::value_ptr(v))) { out << v.r << ' ' << v.g << ' ' << v.b << ' ' << v.a; edited = true; }
        }
        if (edited)
        {
            newValue = out.str();
            if (handle) m_Scripts->SetInstanceField(handle, f.name, newValue); // play mode: live edit, not saved (like Unity)
            else
            {
                MarkEdited();
                stored->value = newValue;
            }
        }
        ImGui::PopID();
    }
    if (handle) ImGui::TextDisabled("Play mode: edits are not saved.");
    ImGui::Spacing();
}

void Editor::DrawAssetInspector()
{
    const std::string& path = m_SelectedAsset;
    std::error_code ec;
    if (!fs::exists(path, ec))
    {
        m_SelectedAsset.clear();
        return;
    }
    const bool dir = fs::is_directory(path, ec);
    ImVec2 p = ImGui::GetCursorScreenPos();
    EditorUI::DrawIcon(ImGui::GetWindowDrawList(), AssetIcon(path, dir), ImVec2(p.x + 16, p.y + 16), 28.0f, IM_COL32(200, 200, 200, 255));
    ImGui::Dummy(ImVec2(34, 32));
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), 0.0f);
    ImGui::TextUnformatted(fs::path(path).filename().string().c_str());
    if (EditorUI::BoldFont()) ImGui::PopFont();
    ImGui::TextDisabled("%s", path.c_str());
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::Spacing();

    if (dir)
    {
        ImGui::TextDisabled("Folder");
    }
    else if (IsMaterial(path))
    {
        DrawMaterialEditor(path);
        ImGui::Spacing();
        ImGui::TextDisabled("Drag this material onto an object in the Scene view or Hierarchy to assign it.");
    }
    else if (ResourceCache::IsTextureFile(path))
    {
        if (Texture* t = m_Res->GetTexture(path, true))
        {
            ImGui::Text("%u x %u", t->width, t->height);
            const float w = ImGui::GetContentRegionAvail().x;
            const float h = w * static_cast<float>(t->height) / std::max(1u, t->width);
            ImGui::Image(m_Res->Thumbnail(path), ImVec2(w, h));
        }
        else
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Unsupported or corrupt image.");
        }
    }
    else if (ResourceCache::IsModelFile(path))
    {
        if (const ModelAsset* model = m_Res->GetModel(path))
        {
            ImGui::Text("Meshes: %d", model->meshCount);
            ImGui::Text("Nodes: %d", static_cast<int>(model->nodes.size()));
            ImGui::SeparatorText("Materials");
            std::vector<std::string> mats = model->meshMaterials;
            std::sort(mats.begin(), mats.end());
            mats.erase(std::unique(mats.begin(), mats.end()), mats.end());
            for (const std::string& m : mats)
                if (!m.empty() && ImGui::Selectable(m.c_str())) SelectAsset(m);
            ImGui::Spacing();
            if (ImGui::Button("Add to Scene", ImVec2(-FLT_MIN, 0))) InstantiateModel(path, kNullEntity, nullptr);
        }
        else
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "Could not import this model (see Console).");
        }
    }
    else if (IsScene(path))
    {
        if (ImGui::Button("Open Scene", ImVec2(-FLT_MIN, 0))) OpenScene(path);
    }
    else if (IsScript(path))
    {
        const std::string cls = fs::path(path).stem().string();
        if (const ScriptClassInfo* info = m_Scripts->FindClass(cls))
            ImGui::TextDisabled("MonoBehaviour with %d inspector field%s", static_cast<int>(info->fields.size()), info->fields.size() == 1 ? "" : "s");
        if (ImGui::Button("Open", ImVec2(-FLT_MIN, 0))) Platform::OpenWithDefaultApp(path);
        // Read-only source preview.
        const auto stamp = fs::last_write_time(path, ec);
        static fs::file_time_type previewStamp;
        if (m_ScriptPreviewPath != path || stamp != previewStamp)
        {
            std::ifstream in(path);
            std::stringstream ss;
            ss << in.rdbuf();
            m_ScriptPreview = ss.str();
            m_ScriptPreviewPath = path;
            previewStamp = stamp;
        }
        ImGui::BeginChild("##source", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_ScriptPreview.c_str());
        ImGui::EndChild();
    }
    else
    {
        ImGui::Text("%llu bytes", static_cast<unsigned long long>(fs::file_size(path, ec)));
        if (ImGui::Button("Open", ImVec2(-FLT_MIN, 0))) Platform::OpenWithDefaultApp(path);
    }
}
