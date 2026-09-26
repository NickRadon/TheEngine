// Hierarchy, Inspector, Lighting, Project and Console panels.
#include "editor/Editor.h"

#include "core/Log.h"
#include "editor/EditorUI.h"

#include <imgui_internal.h>
#include <im_anim.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;
using EditorUI::Icon;

namespace
{
    bool ContainsNoCase(const std::string& haystack, const char* needle)
    {
        if (!needle || !*needle) return true;
        std::string h = haystack, n = needle;
        std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return h.find(n) != std::string::npos;
    }

    Icon EntityIcon(const Entity& e)
    {
        if (e.camera.enabled) return Icon::Camera;
        if (e.reflectionProbe.enabled) return Icon::Sky;
        if (e.volume.enabled) return Icon::Gizmos;
        if (e.light.enabled) return Icon::Sun;
        if (e.meshRenderer.enabled) return Icon::Cube;
        return Icon::Empty;
    }

    // Search box with a magnifier icon (Unity style).
    bool SearchField(const char* id, char* buffer, size_t size, float width)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(width);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        bool changed = ImGui::InputTextWithHint(id, "Search", buffer, size);
        ImGui::PopStyleVar(2);
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), Icon::Search, ImVec2(p.x + 11, p.y + ImGui::GetFrameHeight() * 0.5f),
                           12.0f, IM_COL32(150, 150, 150, 255));
        return changed;
    }
}

void Editor::EntityMenuItems(EntityId parent)
{
    if (ImGui::MenuItem("Create Empty", parent ? nullptr : "Ctrl+Shift+N")) CreateEntity("Empty", parent);
    if (ImGui::BeginMenu("3D Object"))
    {
        for (int i = 1; i < static_cast<int>(PrimitiveType::Count); ++i)
            if (ImGui::MenuItem(PrimitiveName(static_cast<PrimitiveType>(i))))
                CreatePrimitive(static_cast<PrimitiveType>(i), parent);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Light"))
    {
        if (ImGui::MenuItem("Directional Light")) CreateEntity("Directional Light", parent);
        if (ImGui::MenuItem("Point Light")) CreateEntity("Point Light", parent);
        if (ImGui::MenuItem("Spot Light")) CreateEntity("Spot Light", parent);
        ImGui::Separator();
        if (ImGui::MenuItem("Reflection Probe")) CreateEntity("Reflection Probe", parent);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Volume"))
    {
        if (ImGui::MenuItem("Global Volume")) CreateEntity("Global Volume", parent);
        if (ImGui::MenuItem("Box Volume")) CreateEntity("Box Volume", parent);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Camera")) CreateEntity("Camera", parent);
}

// ---------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------
void Editor::DrawHierarchy()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::Begin("Hierarchy");
    ImGui::PopStyleVar();
    if (!open)
    {
        ImGui::End();
        return;
    }

    // Toolbar: "+" create menu and search
    ImGui::SetCursorPos(ImVec2(6, ImGui::GetCursorPosY() + 4));
    if (EditorUI::IconButton("##create", Icon::Plus, false, "Create", ImVec2(28, ImGui::GetFrameHeight())))
        ImGui::OpenPopup("HierarchyCreate");
    if (ImGui::BeginPopup("HierarchyCreate"))
    {
        EntityMenuItems(kNullEntity);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    SearchField("##hsearch", m_HierarchyFilter, sizeof(m_HierarchyFilter), ImGui::GetContentRegionAvail().x - 6);
    ImGui::Separator();
    DrawPrefabModeBar();

    m_HierarchyOrderPrev.swap(m_HierarchyOrder);
    m_HierarchyOrder.clear();
    ImGui::BeginChild("##tree", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));

    // Scene root row
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
    rootFlags |= ImGuiTreeNodeFlags_Framed;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    std::string rootLabel = "      " + m_Scene.name + (m_SceneDirty ? "*" : "");
    if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), 0.0f);
    bool rootOpen = ImGui::TreeNodeEx("##sceneRoot", rootFlags, "%s", rootLabel.c_str());
    if (EditorUI::BoldFont()) ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    {
        ImVec2 min = ImGui::GetItemRectMin();
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), Icon::Scene,
                           ImVec2(min.x + ImGui::GetFontSize() + 16, min.y + ImGui::GetItemRectSize().y * 0.5f), 14.0f,
                           IM_COL32(200, 200, 200, 255));
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            PushUndo();
            m_Scene.SetParent(*static_cast<const EntityId*>(p->Data), kNullEntity);
        }
        ImGui::EndDragDropTarget();
    }

    if (rootOpen)
    {
        if (m_HierarchyFilter[0])
        {
            for (const Entity& e : m_Scene.entities)
                if (ContainsNoCase(e.name, m_HierarchyFilter))
                    DrawHierarchyNode(e.id, m_HierarchyFilter);
        }
        else
        {
            for (EntityId id : m_Scene.Children(kNullEntity))
                DrawHierarchyNode(id, "");
        }
        ImGui::TreePop();
    }
    ImGui::PopStyleVar(2);

    // Empty area: deselect, context menu, drop to root.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##hierarchyEmpty", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 40.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) ClearSelection();
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            PushUndo();
            m_Scene.SetParent(*static_cast<const EntityId*>(p->Data), kNullEntity);
        }
        std::string asset;
        if (AcceptAssetDrop(".glb", asset) || AcceptAssetDrop(".gltf", asset)) InstantiateModel(asset, kNullEntity, nullptr);
        else if (AcceptAssetDrop(".prefab", asset)) InstantiatePrefab(asset, kNullEntity, nullptr);
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("HierarchyEmptyContext"))
    {
        EntityMenuItems(kNullEntity);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::End();
}

void Editor::DrawHierarchyNode(EntityId id, const std::string& filter)
{
    Entity* e = m_Scene.Find(id);
    if (!e) return;
    const std::vector<EntityId> children = filter.empty() ? m_Scene.Children(id) : std::vector<EntityId>{};
    m_HierarchyOrder.push_back(id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NavLeftJumpsToParent;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    const bool selected = IsSelected(id);
    if (selected) flags |= ImGuiTreeNodeFlags_Selected;

    // Reveal entities selected elsewhere (scene view picking, creation).
    if (m_ScrollToEntity != kNullEntity && m_Scene.IsAncestor(id, m_ScrollToEntity))
        ImGui::SetNextItemOpen(true);
    const float rowX = ImGui::GetCursorScreenPos().x; // includes tree indentation
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::PushStyleColor(ImGuiCol_Header, focused ? ImVec4(0.17f, 0.36f, 0.53f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushID(static_cast<int>(id));
    bool open = ImGui::TreeNodeEx("##node", flags, "%s", "");
    ImGui::PopStyleColor();

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const bool toggled = ImGui::IsItemToggledOpen();

    // Selection
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !toggled)
    {
        ImGuiIO& io = ImGui::GetIO();
        auto a = std::find(m_HierarchyOrderPrev.begin(), m_HierarchyOrderPrev.end(), m_LastClicked);
        auto b = std::find(m_HierarchyOrderPrev.begin(), m_HierarchyOrderPrev.end(), id);
        if (io.KeyShift && a != m_HierarchyOrderPrev.end() && b != m_HierarchyOrderPrev.end())
        {
            // Range selection between the last clicked row and this one (anchor is kept).
            if (a > b) std::swap(a, b);
            const EntityId anchor = m_LastClicked;
            m_Selection.assign(a, b + 1);
            std::erase(m_Selection, id);
            m_Selection.push_back(id);
            m_LastClicked = anchor;
        }
        else
        {
            Select(id, io.KeyCtrl);
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !toggled)
    {
        Select(id);
        FrameSelection();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) Select(id);

    // Context menu
    if (ImGui::BeginPopupContextItem("EntityContext"))
    {
        if (ImGui::MenuItem("Rename", "F2"))
        {
            m_RenameEntity = id;
            std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", e->name.c_str());
            m_RenameFocus = true;
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelection();
        if (ImGui::MenuItem("Delete", "Del")) DeleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Frame", "F")) FrameSelection();
        if (const EntityId prefabRoot = Prefab::InstanceRoot(m_Scene, id); prefabRoot && !m_Playing)
        {
            if (ImGui::BeginMenu("Prefab"))
            {
                const std::string asset = m_Scene.Find(prefabRoot)->prefab;
                if (ImGui::MenuItem("Open Asset in Context")) OpenPrefabMode(asset);
                if (ImGui::MenuItem("Select Asset")) SelectAsset(asset);
                ImGui::Separator();
                if (ImGui::MenuItem("Apply All Overrides")) ApplyPrefabOverrides(prefabRoot);
                if (ImGui::MenuItem("Revert All Overrides")) RevertPrefabOverrides(prefabRoot);
                if (ImGui::MenuItem("Unpack")) UnpackPrefab(prefabRoot);
                ImGui::EndMenu();
            }
        }
        else if (!m_Playing && m_PrefabModePath.empty() && ImGui::MenuItem("Create Prefab"))
        {
            CreatePrefab(id, m_ProjectFolder.empty() ? "Assets" : m_ProjectFolder);
        }
        ImGui::Separator();
        EntityMenuItems(id);
        ImGui::EndPopup();
        e = m_Scene.Find(id); // menu actions may have changed the scene
        if (!e)
        {
            if (open) ImGui::TreePop();
            ImGui::PopID();
            return;
        }
    }

    // Drag source
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("ENTITY", &id, sizeof(EntityId));
        ImGui::Text("%s", e->name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target: upper part inserts before (sibling), the rest parents under this entity.
    if (ImGui::BeginDragDropTarget())
    {
        const float rel = (ImGui::GetMousePos().y - itemMin.y) / std::max(itemMax.y - itemMin.y, 1.0f);
        const bool insertBefore = rel < 0.3f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (insertBefore)
            dl->AddLine(ImVec2(itemMin.x, itemMin.y), ImVec2(itemMax.x, itemMin.y), IM_COL32(58, 121, 187, 255), 2.0f);
        else
            dl->AddRect(itemMin, itemMax, IM_COL32(58, 121, 187, 255), 0.0f, 0, 1.5f);

        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
        {
            EntityId dragged = *static_cast<const EntityId*>(p->Data);
            if (dragged != id && !m_Scene.IsAncestor(dragged, id))
            {
                PushUndo();
                if (insertBefore)
                {
                    m_Scene.SetParent(dragged, e->parent);
                    m_Scene.MoveBefore(dragged, id);
                }
                else
                {
                    m_Scene.SetParent(dragged, id);
                    ImGui::GetStateStorage()->SetInt(ImGui::GetItemID(), 1); // open the new parent
                }
            }
        }
        std::string asset;
        if (AcceptAssetDrop(".glb", asset) || AcceptAssetDrop(".gltf", asset)) InstantiateModel(asset, id, nullptr);
        else if (AcceptAssetDrop(".prefab", asset)) InstantiatePrefab(asset, id, nullptr);
        else if (AcceptAssetDrop(".mat", asset)) AssignMaterial(id, asset);
        ImGui::EndDragDropTarget();
        e = m_Scene.Find(id);
        if (!e)
        {
            if (open) ImGui::TreePop();
            ImGui::PopID();
            return;
        }
    }

    // Icon + label (or rename field)
    const bool activeInHierarchy = m_Scene.IsActiveInHierarchy(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = itemMax.y - itemMin.y;
    const float x = rowX + ImGui::GetTreeNodeToLabelSpacing();
    const ImU32 iconCol = activeInHierarchy ? IM_COL32(200, 200, 200, 255) : IM_COL32(120, 120, 120, 255);
    EditorUI::DrawIcon(dl, EntityIcon(*e), ImVec2(x + 7, itemMin.y + h * 0.5f), 14.0f, iconCol);

    if (m_RenameEntity == id)
    {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(x + 18, itemMin.y));
        ImGui::SetNextItemWidth(itemMax.x - x - 22);
        if (m_RenameFocus)
        {
            ImGui::SetKeyboardFocusHere();
            m_RenameFocus = false;
        }
        bool commit = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (commit || ImGui::IsItemDeactivated())
        {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && m_RenameBuffer[0] && e->name != m_RenameBuffer)
            {
                PushUndo();
                m_Scene.Find(id)->name = m_RenameBuffer;
            }
            m_RenameEntity = kNullEntity;
        }
    }
    else
    {
        ImU32 textCol = activeInHierarchy ? IM_COL32(210, 210, 210, 255) : IM_COL32(125, 125, 125, 255);
        if (const EntityId prefabRoot = Prefab::InstanceRoot(m_Scene, id))
        {
            const bool missing = !PrefabContents(m_Scene.Find(prefabRoot)->prefab);
            textCol = missing ? IM_COL32(230, 110, 110, activeInHierarchy ? 255 : 150)
                              : IM_COL32(125, 178, 255, activeInHierarchy ? 255 : 150);
        }
        dl->AddText(ImVec2(x + 18, itemMin.y + (h - ImGui::GetFontSize()) * 0.5f), textCol, e->name.c_str());
    }

    if (m_ScrollToEntity == id)
    {
        ImGui::SetScrollHereY(0.5f);
        m_ScrollToEntity = kNullEntity;
    }

    if (open)
    {
        for (EntityId child : children) DrawHierarchyNode(child, filter);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------
void Editor::DrawInspector()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();
        return;
    }
    Entity* e = m_Scene.Find(ActiveEntity());
    if (!e)
    {
        if (!m_SelectedAsset.empty()) DrawAssetInspector();
        ImGui::End();
        return;
    }
    ImGui::PushID(static_cast<int>(e->id));

    // Header: icon, active toggle, name, static
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), EntityIcon(*e), ImVec2(p.x + 14, p.y + 16), 26.0f, IM_COL32(190, 190, 190, 255));
        ImGui::Dummy(ImVec2(32, 32));
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Checkbox("##active", &e->active)) MarkEdited();
        ImGui::SameLine();
        char name[128];
        std::snprintf(name, sizeof(name), "%s", e->name.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
        if (ImGui::InputText("##name", name, sizeof(name)))
        {
            MarkEdited();
            e->name = name;
        }
        ImGui::SameLine();
        static bool isStatic = false;
        ImGui::Checkbox("Static", &isStatic);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Tag");
        ImGui::SameLine();
        const float half = (ImGui::GetContentRegionAvail().x - 80) * 0.5f;
        ImGui::SetNextItemWidth(half);
        if (ImGui::BeginCombo("##tag", "Untagged")) { ImGui::Selectable("Untagged", true); ImGui::EndCombo(); }
        ImGui::SameLine();
        ImGui::TextUnformatted("Layer");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##layer", "Default")) { ImGui::Selectable("Default", true); ImGui::EndCombo(); }
        ImGui::EndGroup();
        if (m_Selection.size() > 1)
            ImGui::TextDisabled("%d objects selected (editing the active one)", static_cast<int>(m_Selection.size()));
    }
    ImGui::Spacing();
    DrawPrefabInstanceHeader(e->id);
    e = m_Scene.Find(ActiveEntity()); // the prefab header can change the scene
    if (!e)
    {
        ImGui::PopID();
        ImGui::End();
        return;
    }

    // Transform
    if (EditorUI::ComponentHeader("Transform", Icon::Move, nullptr, nullptr))
    {
        Transform& t = e->transform;
        if (EditorUI::Vec3Field("Position", glm::value_ptr(t.position), 0.05f, 0.0f)) MarkEdited();
        glm::vec3 euler = t.euler;
        if (EditorUI::Vec3Field("Rotation", glm::value_ptr(euler), 0.5f, 0.0f))
        {
            MarkEdited();
            t.SetEuler(euler);
        }
        if (EditorUI::Vec3Field("Scale", glm::value_ptr(t.scale), 0.02f, 1.0f)) MarkEdited();
        ImGui::Spacing();
    }

    // Mesh Filter + Mesh Renderer
    if (e->meshRenderer.enabled)
    {
        bool remove = false;
        MeshRendererComponent& m = e->meshRenderer;
        const size_t hash = m.mesh.rfind('#');
        const std::string modelPath = hash != std::string::npos ? m.mesh.substr(0, hash) : std::string();
        auto meshLabel = [](const std::string& ref) {
            const size_t h = ref.rfind('#');
            return h == std::string::npos ? ref : fs::path(ref.substr(0, h)).stem().string() + " [" + ref.substr(h + 1) + "]";
        };
        std::string filterLabel = meshLabel(m.mesh) + " (Mesh Filter)";
        if (EditorUI::ComponentHeader(filterLabel.c_str(), Icon::Cube, nullptr, nullptr))
        {
            EditorUI::PropertyLabel("Mesh");
            if (ImGui::BeginCombo("##mesh", meshLabel(m.mesh).c_str()))
            {
                for (int i = 1; i < static_cast<int>(PrimitiveType::Count); ++i)
                {
                    const char* name = PrimitiveName(static_cast<PrimitiveType>(i));
                    if (ImGui::Selectable(name, m.mesh == name))
                    {
                        MarkEdited();
                        m.mesh = name;
                    }
                }
                if (!modelPath.empty())
                    if (const ModelAsset* model = m_Res->GetModel(modelPath))
                    {
                        ImGui::Separator();
                        for (int i = 0; i < model->meshCount; ++i)
                        {
                            const std::string ref = modelPath + "#" + std::to_string(i);
                            if (ImGui::Selectable(meshLabel(ref).c_str(), m.mesh == ref))
                            {
                                MarkEdited();
                                m.mesh = ref;
                            }
                        }
                    }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
        }
        if (EditorUI::ComponentHeader("Mesh Renderer", Icon::Sky, nullptr, &remove))
        {
            EditorUI::PropertyLabel("Cast Shadows");
            if (ImGui::Checkbox("##castShadows", &m.castShadows)) MarkEdited();

            // Material slot: click to pick, or drag a .mat from the Project window.
            EditorUI::PropertyLabel("Material");
            const std::string matLabel = m.material.empty() ? "None (inline properties)" : fs::path(m.material).stem().string();
            const float pickW = ImGui::GetContentRegionAvail().x - 28.0f;
            if (ImGui::Button(matLabel.c_str(), ImVec2(pickW, 0))) ImGui::OpenPopup("MaterialPicker");
            if (ImGui::IsItemHovered() && !m.material.empty()) ImGui::SetTooltip("%s", m.material.c_str());
            if (ImGui::BeginDragDropTarget())
            {
                std::string dropped;
                if (AcceptAssetDrop(".mat", dropped))
                {
                    MarkEdited();
                    m.material = dropped;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(0, 4);
            ImGui::BeginDisabled(m.material.empty());
            if (EditorUI::IconButton("##selectMat", Icon::Search, false, "Select the material asset", ImVec2(24, ImGui::GetFrameHeight())))
                SelectAsset(m.material);
            ImGui::EndDisabled();
            if (ImGui::BeginPopup("MaterialPicker"))
            {
                if (ImGui::Selectable("None (inline properties)", m.material.empty()))
                {
                    MarkEdited();
                    m.material.clear();
                }
                ImGui::Separator();
                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator("Assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    if (it->path().extension() != ".mat") continue;
                    const std::string path = it->path().generic_string();
                    if (ImGui::Selectable(path.c_str(), m.material == path))
                    {
                        MarkEdited();
                        m.material = path;
                    }
                }
                ImGui::EndPopup();
            }

            if (m.material.empty())
            {
                EditorUI::PropertyLabel("Albedo");
                if (ImGui::ColorEdit3("##albedo", glm::value_ptr(m.color))) MarkEdited();
                EditorUI::PropertyLabel("Metallic");
                if (ImGui::SliderFloat("##metallic", &m.metallic, 0.0f, 1.0f)) MarkEdited();
                EditorUI::PropertyLabel("Smoothness");
                if (ImGui::SliderFloat("##smoothness", &m.smoothness, 0.0f, 1.0f)) MarkEdited();
            }
            ImGui::Spacing();
        }
        if (!m.material.empty())
        {
            // Unity shows the material's inspector at the bottom of the GameObject inspector.
            const std::string header = fs::path(m.material).stem().string() + " (Material)";
            if (EditorUI::ComponentHeader(header.c_str(), Icon::Sky, nullptr, nullptr, false))
            {
                DrawMaterialEditor(m.material);
                ImGui::Spacing();
            }
        }
        if (remove)
        {
            PushUndo();
            e->meshRenderer.enabled = false;
        }
    }

    // Light
    if (e->light.enabled)
    {
        bool remove = false;
        if (EditorUI::ComponentHeader("Light", Icon::Sun, nullptr, &remove))
        {
            LightComponent& l = e->light;
            EditorUI::PropertyLabel("Type");
            int type = static_cast<int>(l.type);
            if (ImGui::Combo("##type", &type, "Directional\0Point\0Spot\0"))
            {
                MarkEdited();
                l.type = static_cast<LightType>(type);
            }
            if (l.type != LightType::Directional)
            {
                EditorUI::PropertyLabel("Range");
                if (ImGui::DragFloat("##range", &l.range, 0.05f, 0.01f, 1000.0f)) MarkEdited();
            }
            if (l.type == LightType::Spot)
            {
                EditorUI::PropertyLabel("Spot Angle");
                if (ImGui::SliderFloat("##spot", &l.spotAngle, 1.0f, 179.0f, "%.1f")) MarkEdited();
                EditorUI::PropertyLabel("Inner Spot Angle");
                if (ImGui::SliderFloat("##innerSpot", &l.innerSpotAngle, 0.0f, l.spotAngle, "%.1f")) MarkEdited();
            }
            EditorUI::PropertyLabel("Color");
            if (ImGui::ColorEdit3("##color", glm::value_ptr(l.color))) MarkEdited();
            EditorUI::PropertyLabel("Intensity");
            if (ImGui::DragFloat("##intensity", &l.intensity, 0.01f, 0.0f, 100.0f)) MarkEdited();
            EditorUI::PropertyLabel("Shadow Type");
            int shadowType = l.castShadows ? 1 : 0;
            if (ImGui::Combo("##shadowType", &shadowType, "No Shadows\0Soft Shadows\0"))
            {
                MarkEdited();
                l.castShadows = shadowType == 1;
            }
            if (l.castShadows)
            {
                EditorUI::PropertyLabel("Strength");
                if (ImGui::SliderFloat("##shadowStrength", &l.shadowStrength, 0.0f, 1.0f)) MarkEdited();
            }
            if (l.type == LightType::Directional)
                ImGui::TextDisabled("The first directional light drives the procedural sky's sun.");
            else if (l.castShadows)
                ImGui::TextDisabled(l.type == LightType::Point ? "Point light shadows use 6 of the 24 shadow layers."
                                                               : "Spot light shadows use 1 of the 24 shadow layers.");
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->light.enabled = false;
        }
    }

    // Camera
    if (e->camera.enabled)
    {
        bool remove = false;
        if (EditorUI::ComponentHeader("Camera", Icon::Camera, nullptr, &remove))
        {
            CameraComponent& c = e->camera;
            EditorUI::PropertyLabel("Projection");
            int proj = c.orthographic ? 1 : 0;
            if (ImGui::Combo("##projection", &proj, "Perspective\0Orthographic\0"))
            {
                MarkEdited();
                c.orthographic = proj == 1;
            }
            if (c.orthographic)
            {
                if (ImGui::DragFloat("##size", &c.orthoSize, 0.05f, 0.01f, 1000.0f)) MarkEdited();
            }
            else
            {
                EditorUI::PropertyLabel("Field of View");
                if (ImGui::SliderFloat("##fov", &c.fov, 1.0f, 179.0f, "%.1f")) MarkEdited();
            }
            EditorUI::PropertyLabel("Clipping Near");
            if (ImGui::DragFloat("##near", &c.nearClip, 0.01f, 0.01f, c.farClip - 0.01f)) MarkEdited();
            EditorUI::PropertyLabel("Clipping Far");
            if (ImGui::DragFloat("##far", &c.farClip, 1.0f, c.nearClip + 0.01f, 100000.0f)) MarkEdited();
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->camera.enabled = false;
        }
    }

    // Rigidbody
    if (e->rigidbody.enabled)
    {
        bool remove = false;
        if (EditorUI::ComponentHeader("Rigidbody", Icon::Gizmos, nullptr, &remove))
        {
            RigidbodyComponent& r = e->rigidbody;
            EditorUI::PropertyLabel("Mass");
            if (ImGui::DragFloat("##mass", &r.mass, 0.05f, 0.0001f, 100000.0f)) MarkEdited();
            EditorUI::PropertyLabel("Drag");
            if (ImGui::DragFloat("##drag", &r.drag, 0.01f, 0.0f, 1000.0f)) MarkEdited();
            EditorUI::PropertyLabel("Angular Drag");
            if (ImGui::DragFloat("##angularDrag", &r.angularDrag, 0.01f, 0.0f, 1000.0f)) MarkEdited();
            EditorUI::PropertyLabel("Use Gravity");
            if (ImGui::Checkbox("##useGravity", &r.useGravity)) MarkEdited();
            EditorUI::PropertyLabel("Is Kinematic");
            if (ImGui::Checkbox("##isKinematic", &r.isKinematic)) MarkEdited();
            if (m_Playing && m_Physics.Running() && !r.isKinematic)
            {
                const glm::vec3 v = m_Physics.GetVelocity(e->id);
                ImGui::TextDisabled("Velocity  %.2f  %.2f  %.2f", v.x, v.y, v.z);
            }
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->rigidbody.enabled = false;
        }
    }

    // Collider
    if (e->collider.enabled)
    {
        bool remove = false;
        static const char* kColliderNames[] = { "Box Collider", "Sphere Collider", "Capsule Collider", "Mesh Collider" };
        if (EditorUI::ComponentHeader(kColliderNames[static_cast<int>(e->collider.shape)], Icon::Cube, nullptr, &remove))
        {
            ColliderComponent& c = e->collider;
            EditorUI::PropertyLabel("Shape");
            int shape = static_cast<int>(c.shape);
            if (ImGui::Combo("##shape", &shape, "Box\0Sphere\0Capsule\0Mesh\0"))
            {
                MarkEdited();
                FitCollider(*e, static_cast<ColliderShape>(shape));
            }
            EditorUI::PropertyLabel("Is Trigger");
            if (ImGui::Checkbox("##isTrigger", &c.isTrigger)) MarkEdited();
            if (c.shape != ColliderShape::Mesh)
            {
                if (EditorUI::Vec3Field("Center", glm::value_ptr(c.center), 0.01f, 0.0f)) MarkEdited();
            }
            if (c.shape == ColliderShape::Box)
            {
                if (EditorUI::Vec3Field("Size", glm::value_ptr(c.size), 0.01f, 1.0f)) MarkEdited();
            }
            if (c.shape == ColliderShape::Sphere || c.shape == ColliderShape::Capsule)
            {
                EditorUI::PropertyLabel("Radius");
                if (ImGui::DragFloat("##radius", &c.radius, 0.01f, 0.001f, 10000.0f)) MarkEdited();
            }
            if (c.shape == ColliderShape::Capsule)
            {
                EditorUI::PropertyLabel("Height");
                if (ImGui::DragFloat("##height", &c.height, 0.01f, 0.001f, 10000.0f)) MarkEdited();
            }
            if (c.shape == ColliderShape::Mesh)
            {
                ImGui::TextDisabled(e->meshRenderer.enabled ? "Uses the Mesh Renderer's mesh (a convex hull on a\nnon-kinematic Rigidbody)."
                                                            : "Needs a Mesh Renderer to take the mesh from.");
            }
            EditorUI::PropertyLabel("Friction");
            if (ImGui::SliderFloat("##friction", &c.friction, 0.0f, 1.0f)) MarkEdited();
            EditorUI::PropertyLabel("Bounciness");
            if (ImGui::SliderFloat("##bounciness", &c.bounciness, 0.0f, 1.0f)) MarkEdited();
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->collider.enabled = false;
        }
    }

    // Animator
    if (e->animator.enabled)
    {
        bool remove = false;
        if (EditorUI::ComponentHeader("Animator", Icon::Play, nullptr, &remove))
        {
            AnimatorComponent& a = e->animator;
            EditorUI::PropertyLabel("Controller");
            const std::string label = a.controller.empty() ? "None (Animator Controller)" : std::filesystem::path(a.controller).stem().string();
            const float bw = ImGui::GetContentRegionAvail().x;
            if (ImGui::Button(label.c_str(), ImVec2(bw - 104, 0)) && !a.controller.empty()) OpenAnimatorController(a.controller);
            if (!a.controller.empty()) ImGui::SetItemTooltip("%s (click to open in the Animator window)", a.controller.c_str());
            if (ImGui::BeginDragDropTarget())
            {
                std::string asset;
                if (AcceptAssetDrop(".controller", asset)) { MarkEdited(); a.controller = asset; }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(0, 4);
            if (ImGui::Button("New", ImVec2(48, 0)))
            {
                const std::string path = CreateAsset("Assets/Animators", "controller");
                if (!path.empty())
                {
                    MarkEdited();
                    m_Scene.Find(ActiveEntity())->animator.controller = path;
                    OpenAnimatorController(path);
                }
                e = m_Scene.Find(ActiveEntity());
            }
            ImGui::SameLine(0, 4);
            if (ImGui::Button("Open", ImVec2(48, 0)) && !e->animator.controller.empty()) OpenAnimatorController(e->animator.controller);
            EditorUI::PropertyLabel("Apply Root Motion");
            if (ImGui::Checkbox("##rootMotion", &e->animator.applyRootMotion)) MarkEdited();
            if (m_Playing)
                if (AnimatorInstance* inst = m_Animation.Instance(e->id))
                {
                    const AnimatorController* ctrl = inst->Controller();
                    const int cur = inst->CurrentState();
                    ImGui::TextDisabled("State: %s%s%s", cur >= 0 ? ctrl->states[cur].name.c_str() : "-",
                                        inst->NextState() >= 0 ? " -> " : "", inst->NextState() >= 0 ? ctrl->states[inst->NextState()].name.c_str() : "");
                }
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->animator.enabled = false;
        }
    }

    // Reflection Probe
    if (e->reflectionProbe.enabled)
    {
        bool remove = false;
        if (EditorUI::ComponentHeader("Reflection Probe", Icon::Sky, nullptr, &remove))
        {
            ReflectionProbeComponent& p = e->reflectionProbe;
            if (EditorUI::Vec3Field("Box Size", glm::value_ptr(p.size), 0.05f, 10.0f)) MarkEdited();
            EditorUI::PropertyLabel("Box Projection");
            if (ImGui::Checkbox("##boxProjection", &p.boxProjection)) MarkEdited();
            EditorUI::PropertyLabel("Intensity");
            if (ImGui::DragFloat("##probeIntensity", &p.intensity, 0.01f, 0.0f, 10.0f)) MarkEdited();
            EditorUI::PropertyLabel("");
            if (ImGui::Button("Bake")) m_ProbeSignatures.erase(e->id);
            ImGui::SameLine();
            ImGui::TextDisabled(m_Renderer->IsProbeBaked(e->id) ? "Baked (updates automatically)" : "Not baked yet");
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->reflectionProbe.enabled = false;
        }
    }

    // Volume (post-processing)
    if (e->volume.enabled)
    {
        bool remove = false;
        if (EditorUI::ComponentHeader("Volume", Icon::Gizmos, nullptr, &remove))
        {
            VolumeComponent& v = e->volume;
            EditorUI::PropertyLabel("Mode");
            int mode = v.isGlobal ? 0 : 1;
            if (ImGui::Combo("##volumeMode", &mode, "Global\0Local\0")) { MarkEdited(); v.isGlobal = mode == 0; }
            if (!v.isGlobal)
            {
                if (EditorUI::Vec3Field("Box Size", glm::value_ptr(v.size), 0.05f, 10.0f)) MarkEdited();
                EditorUI::PropertyLabel("Blend Distance");
                if (ImGui::DragFloat("##blend", &v.blendDistance, 0.05f, 0.0f, 1000.0f)) MarkEdited();
            }
            EditorUI::PropertyLabel("Weight");
            if (ImGui::SliderFloat("##weight", &v.weight, 0.0f, 1.0f)) MarkEdited();
            EditorUI::PropertyLabel("Priority");
            if (ImGui::DragFloat("##priority", &v.priority, 0.1f)) MarkEdited();

            PostProcessSettings& s = v.settings;
            auto group = [&](const char* name, bool& active) {
                ImGui::Spacing();
                ImGui::PushID(name);
                if (ImGui::Checkbox("##active", &active)) MarkEdited();
                ImGui::SameLine();
                const bool open = ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopID();
                if (open && !active) ImGui::BeginDisabled();
                return open;
            };
            auto endGroup = [&](bool open, bool active) {
                if (!open) return;
                if (!active) ImGui::EndDisabled();
                ImGui::TreePop();
            };
            ImGui::SeparatorText("Overrides");
            if (bool open = group("Bloom", s.bloom))
            {
                EditorUI::PropertyLabel("Threshold");
                if (ImGui::DragFloat("##bThreshold", &s.bloomThreshold, 0.01f, 0.0f, 10.0f)) MarkEdited();
                EditorUI::PropertyLabel("Intensity");
                if (ImGui::DragFloat("##bIntensity", &s.bloomIntensity, 0.01f, 0.0f, 10.0f)) MarkEdited();
                EditorUI::PropertyLabel("Scatter");
                if (ImGui::SliderFloat("##bScatter", &s.bloomScatter, 0.0f, 1.0f)) MarkEdited();
                EditorUI::PropertyLabel("Tint");
                if (ImGui::ColorEdit3("##bTint", glm::value_ptr(s.bloomTint))) MarkEdited();
                endGroup(open, s.bloom);
            }
            if (bool open = group("Color Adjustments", s.colorAdjustments))
            {
                EditorUI::PropertyLabel("Post Exposure");
                if (ImGui::DragFloat("##postExposure", &s.postExposure, 0.01f, -10.0f, 10.0f, "%.2f EV")) MarkEdited();
                EditorUI::PropertyLabel("Contrast");
                if (ImGui::SliderFloat("##contrast", &s.contrast, -100.0f, 100.0f, "%.0f")) MarkEdited();
                EditorUI::PropertyLabel("Color Filter");
                if (ImGui::ColorEdit3("##filter", glm::value_ptr(s.colorFilter))) MarkEdited();
                EditorUI::PropertyLabel("Saturation");
                if (ImGui::SliderFloat("##saturation", &s.saturation, -100.0f, 100.0f, "%.0f")) MarkEdited();
                endGroup(open, s.colorAdjustments);
            }
            if (bool open = group("White Balance", s.whiteBalance))
            {
                EditorUI::PropertyLabel("Temperature");
                if (ImGui::SliderFloat("##temperature", &s.temperature, -100.0f, 100.0f, "%.0f")) MarkEdited();
                EditorUI::PropertyLabel("Tint");
                if (ImGui::SliderFloat("##tint", &s.tint, -100.0f, 100.0f, "%.0f")) MarkEdited();
                endGroup(open, s.whiteBalance);
            }
            if (bool open = group("Vignette", s.vignette))
            {
                EditorUI::PropertyLabel("Color");
                if (ImGui::ColorEdit3("##vColor", glm::value_ptr(s.vignetteColor))) MarkEdited();
                EditorUI::PropertyLabel("Intensity");
                if (ImGui::SliderFloat("##vIntensity", &s.vignetteIntensity, 0.0f, 1.0f)) MarkEdited();
                EditorUI::PropertyLabel("Smoothness");
                if (ImGui::SliderFloat("##vSmooth", &s.vignetteSmoothness, 0.01f, 1.0f)) MarkEdited();
                endGroup(open, s.vignette);
            }
            if (bool open = group("Tonemapping", s.tonemapping))
            {
                EditorUI::PropertyLabel("Mode");
                if (ImGui::Combo("##tonemapper", &s.tonemapper, "None\0ACES\0Neutral\0")) MarkEdited();
                endGroup(open, s.tonemapping);
            }
            ImGui::Spacing();
        }
        if (remove)
        {
            PushUndo();
            e->volume.enabled = false;
        }
    }

    // C# scripts
    for (size_t i = 0; i < e->scripts.size(); ++i)
    {
        bool removed = false;
        ImGui::PushID(static_cast<int>(i) + 1000);
        DrawScriptComponent(*e, i, removed);
        ImGui::PopID();
        if (removed)
        {
            PushUndo();
            m_Scene.Find(e->id)->scripts.erase(m_Scene.Find(e->id)->scripts.begin() + static_cast<long>(i));
            e = m_Scene.Find(ActiveEntity());
            break;
        }
    }

    // Add Component
    ImGui::Separator();
    ImGui::Spacing();
    const float bw = std::min(230.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - bw) * 0.5f + ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Add Component", ImVec2(bw, 0)))
    {
        m_AddComponentFilter[0] = 0;
        ImGui::OpenPopup("AddComponent");
    }
    ImGui::SetNextWindowSize(ImVec2(bw, 0));
    if (ImGui::BeginPopup("AddComponent"))
    {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        SearchField("##acsearch", m_AddComponentFilter, sizeof(m_AddComponentFilter), -FLT_MIN);
        ImGui::Separator();
        struct Item { const char* name; bool* flag; };
        Item items[] = {
            { "Mesh Renderer", &e->meshRenderer.enabled },
            { "Light", &e->light.enabled },
            { "Camera", &e->camera.enabled },
            { "Animator", &e->animator.enabled },
            { "Reflection Probe", &e->reflectionProbe.enabled },
            { "Volume", &e->volume.enabled },
        };
        for (const Item& item : items)
        {
            if (*item.flag || !ContainsNoCase(item.name, m_AddComponentFilter)) continue;
            if (ImGui::Selectable(item.name))
            {
                PushUndo();
                *item.flag = true;
            }
        }
        ImGui::SeparatorText("Physics");
        if (!e->rigidbody.enabled && ContainsNoCase("Rigidbody", m_AddComponentFilter) && ImGui::Selectable("Rigidbody"))
        {
            PushUndo();
            e->rigidbody = RigidbodyComponent{};
            e->rigidbody.enabled = true;
        }
        const char* colliders[] = { "Box Collider", "Sphere Collider", "Capsule Collider", "Mesh Collider" };
        for (int i = 0; i < 4 && !e->collider.enabled; ++i)
        {
            if (!ContainsNoCase(colliders[i], m_AddComponentFilter)) continue;
            if (ImGui::Selectable(colliders[i]))
            {
                PushUndo();
                e->collider = ColliderComponent{};
                e->collider.enabled = true;
                FitCollider(*e, static_cast<ColliderShape>(i));
            }
        }
        if (!m_Scripts->Classes().empty())
        {
            ImGui::SeparatorText("Scripts");
            for (const ScriptClassInfo& c : m_Scripts->Classes())
            {
                if (!ContainsNoCase(c.name, m_AddComponentFilter)) continue;
                if (ImGui::Selectable((c.name + " (Script)").c_str()))
                {
                    PushUndo();
                    ScriptComponent sc;
                    sc.className = c.name;
                    for (const ScriptFieldInfo& f : c.fields) sc.fields.push_back({ f.name, f.type, f.defaultValue });
                    m_Scene.Find(ActiveEntity())->scripts.push_back(sc);
                }
            }
        }
        if (ImGui::Selectable("New script..."))
        {
            std::string path = CreateAsset("Assets/Scripts", "script");
            if (!path.empty())
                Notify("Created " + path + " (it will be added after it compiles)");
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Lighting (Window > Rendering > Lighting): procedural skybox settings
// ---------------------------------------------------------------------------
void Editor::DrawLighting()
{
    if (!ImGui::Begin("Lighting"))
    {
        ImGui::End();
        return;
    }
    SkySettings& s = m_Scene.sky;

    if (EditorUI::ComponentHeader("Environment", Icon::Sky, nullptr, nullptr))
    {
        EditorUI::PropertyLabel("Skybox");
        int mode = s.enabled ? 0 : 1;
        if (ImGui::Combo("##skymode", &mode, "Procedural Sky\0Solid Color\0"))
        {
            MarkEdited();
            s.enabled = mode == 0;
        }
        if (!s.enabled)
        {
            EditorUI::PropertyLabel("Background");
            if (ImGui::ColorEdit3("##bg", glm::value_ptr(s.fallbackColor))) MarkEdited();
        }

        // Sun source
        const Entity* sun = nullptr;
        for (const Entity& e : m_Scene.entities)
            if (e.light.enabled && m_Scene.IsActiveInHierarchy(e.id)) { sun = &e; break; }
        EditorUI::PropertyLabel("Sun Source");
        ImGui::BeginDisabled();
        char sunName[128];
        std::snprintf(sunName, sizeof(sunName), "%s", sun ? sun->name.c_str() : "None (Light)");
        ImGui::InputText("##sun", sunName, sizeof(sunName), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();

        // Time of day: rotates the sun light. Presets animate with ImAnim.
        if (sun)
        {
            Entity* sunEntity = m_Scene.Find(sun->id);
            static float animTarget = -1.0f;
            glm::vec3 dir = glm::normalize(glm::vec3(m_Scene.WorldMatrix(sun->id) * glm::vec4(0, 0, 1, 0))); // towards sun
            float elevation = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
            // Map the light pitch to a 24h clock: pitch -90 = noon, 0 and -180 = horizon.
            float hour = std::fmod(12.0f + (sunEntity->transform.euler.x + 90.0f) / 15.0f, 24.0f);
            if (hour < 0.0f) hour += 24.0f;

            if (animTarget >= 0.0f)
            {
                float animated = iam_tween_float(ImHashStr("Lighting"), ImHashStr("hour"), animTarget, 1.2f,
                                                 iam_ease_preset(iam_ease_in_out_cubic), iam_policy_crossfade,
                                                 ImGui::GetIO().DeltaTime, hour);
                if (std::fabs(animated - animTarget) < 0.01f) animTarget = -1.0f;
                hour = animated;
                glm::vec3 eul = sunEntity->transform.euler;
                eul.x = (hour - 12.0f) * 15.0f - 90.0f;
                sunEntity->transform.SetEuler(eul);
            }

            EditorUI::PropertyLabel("Time of Day");
            float h = hour;
            if (ImGui::SliderFloat("##tod", &h, 0.0f, 24.0f, "%.1f h"))
            {
                MarkEdited();
                glm::vec3 eul = sunEntity->transform.euler;
                eul.x = (h - 12.0f) * 15.0f - 90.0f;
                sunEntity->transform.SetEuler(eul);
                animTarget = -1.0f;
            }
            ImGui::TextDisabled("Sun elevation: %.1f deg", elevation);
            const float bw = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
            struct Preset { const char* name; float hour; };
            for (Preset p : { Preset{ "Sunrise", 6.3f }, Preset{ "Noon", 12.0f }, Preset{ "Sunset", 17.7f }, Preset{ "Night", 22.0f } })
            {
                if (ImGui::Button(p.name, ImVec2(bw, 0)))
                {
                    MarkEdited();
                    iam_tween_float(ImHashStr("Lighting"), ImHashStr("hour"), hour, 0.0f, iam_ease_preset(iam_ease_linear),
                                    iam_policy_cut, 0.0f, hour);
                    animTarget = p.hour;
                }
                if (p.hour < 22.0f) ImGui::SameLine();
            }
        }
        ImGui::Spacing();
    }

    if (EditorUI::ComponentHeader("Procedural Skybox", Icon::Sun, nullptr, nullptr))
    {
        auto slider = [&](const char* label, const char* id, float* v, float mn, float mx, const char* fmt = "%.3f") {
            EditorUI::PropertyLabel(label);
            if (ImGui::SliderFloat(id, v, mn, mx, fmt)) MarkEdited();
        };
        slider("Sun Size", "##sunsize", &s.sunSize, 0.0f, 1.0f);
        slider("Sun Convergence", "##sunconv", &s.sunConvergence, 1.0f, 10.0f, "%.1f");
        slider("Atmosphere Thickness", "##atmo", &s.atmosphereThickness, 0.0f, 5.0f, "%.2f");
        EditorUI::PropertyLabel("Sky Tint");
        if (ImGui::ColorEdit3("##tint", glm::value_ptr(s.skyTint))) MarkEdited();
        EditorUI::PropertyLabel("Ground");
        if (ImGui::ColorEdit3("##ground", glm::value_ptr(s.groundColor))) MarkEdited();
        slider("Exposure", "##exposure", &s.exposure, 0.0f, 8.0f, "%.2f");
        slider("Ambient Intensity", "##ambient", &s.ambientIntensity, 0.0f, 4.0f, "%.2f");
        slider("Shadow Distance", "##shadowDistance", &s.shadowDistance, 0.0f, 500.0f, "%.0f m");
        slider("Reflection Intensity", "##reflections", &s.reflectionIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::Spacing();
    }

    if (EditorUI::ComponentHeader("Ambient Occlusion (SSAO)", Icon::Gizmos, &s.ssao, nullptr))
    {
        EditorUI::PropertyLabel("Intensity");
        if (ImGui::SliderFloat("##aoIntensity", &s.ssaoIntensity, 0.0f, 4.0f, "%.2f")) MarkEdited();
        EditorUI::PropertyLabel("Radius");
        if (ImGui::SliderFloat("##aoRadius", &s.ssaoRadius, 0.05f, 3.0f, "%.2f m")) MarkEdited();
        ImGui::Spacing();
    }

    if (EditorUI::ComponentHeader("Clouds & Stars", Icon::Sky, nullptr, nullptr))
    {
        auto slider = [&](const char* label, const char* id, float* v, float mn, float mx) {
            EditorUI::PropertyLabel(label);
            if (ImGui::SliderFloat(id, v, mn, mx, "%.2f")) MarkEdited();
        };
        slider("Cloud Coverage", "##cov", &s.cloudCoverage, 0.0f, 1.0f);
        slider("Cloud Density", "##den", &s.cloudDensity, 0.0f, 1.0f);
        slider("Cloud Speed", "##spd", &s.cloudSpeed, 0.0f, 10.0f);
        slider("Cloud Scale", "##scl", &s.cloudScale, 0.05f, 3.0f);
        slider("Stars", "##stars", &s.stars, 0.0f, 4.0f);
        ImGui::Spacing();
    }

    if (ImGui::Button("Reset Sky Settings", ImVec2(-FLT_MIN, 0)))
    {
        PushUndo();
        s = SkySettings();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
void Editor::DrawConsole()
{
    if (!ImGui::Begin("Console"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) Log::Clear();
    ImGui::SameLine();
    EditorUI::ToggleButton("Collapse", &m_ConsoleCollapse);

    // Right side: per-level toggles with counts (Unity style).
    auto levelToggle = [&](const char* id, Icon icon, ImU32 col, int count, bool* show) {
        char label[32];
        std::snprintf(label, sizeof(label), "     %d%s", count, id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        EditorUI::ToggleButton(label, show);
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(p.x + 13, p.y + ImGui::GetFrameHeight() * 0.5f), 13.0f, col);
    };
    const float rightW = 200.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - rightW));
    levelToggle("##i", Icon::Info, IM_COL32(200, 200, 200, 255), Log::CountOf(LogLevel::Info), &m_ConsoleShowInfo);
    ImGui::SameLine();
    levelToggle("##w", Icon::Warning, IM_COL32(240, 190, 60, 255), Log::CountOf(LogLevel::Warning), &m_ConsoleShowWarn);
    ImGui::SameLine();
    levelToggle("##e", Icon::Error, IM_COL32(230, 80, 70, 255), Log::CountOf(LogLevel::Error), &m_ConsoleShowError);
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const auto& entries = Log::Entries();
    const float rowH = ImGui::GetTextLineHeight() * 2.0f + 6.0f;
    int row = 0;
    for (const LogEntry& entry : entries)
    {
        if (entry.level == LogLevel::Info && !m_ConsoleShowInfo) continue;
        if (entry.level == LogLevel::Warning && !m_ConsoleShowWarn) continue;
        if (entry.level == LogLevel::Error && !m_ConsoleShowError) continue;
        const int repeats = m_ConsoleCollapse ? 1 : entry.count;
        for (int r = 0; r < repeats; ++r, ++row)
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (row % 2) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), IM_COL32(255, 255, 255, 6));
            Icon icon = entry.level == LogLevel::Error ? Icon::Error : entry.level == LogLevel::Warning ? Icon::Warning : Icon::Info;
            ImU32 col = entry.level == LogLevel::Error ? IM_COL32(230, 80, 70, 255)
                      : entry.level == LogLevel::Warning ? IM_COL32(240, 190, 60, 255) : IM_COL32(200, 200, 200, 255);
            EditorUI::DrawIcon(dl, icon, ImVec2(p.x + 16, p.y + rowH * 0.5f), 20.0f, col);
            char when[32];
            std::snprintf(when, sizeof(when), "[%02d:%02d:%02d] ", static_cast<int>(entry.time) / 3600,
                          (static_cast<int>(entry.time) / 60) % 60, static_cast<int>(entry.time) % 60);
            dl->AddText(ImVec2(p.x + 34, p.y + 3), IM_COL32(150, 150, 150, 255), when);
            dl->AddText(ImVec2(p.x + 34 + ImGui::CalcTextSize(when).x, p.y + 3), IM_COL32(215, 215, 215, 255), entry.message.c_str());
            dl->AddText(ImVec2(p.x + 34, p.y + 3 + ImGui::GetTextLineHeight()), IM_COL32(120, 120, 120, 255), "TheEngine");
            if (m_ConsoleCollapse && entry.count > 1)
            {
                char badge[16];
                std::snprintf(badge, sizeof(badge), "%d", entry.count);
                ImVec2 bs = ImGui::CalcTextSize(badge);
                ImVec2 bmin(p.x + w - bs.x - 20, p.y + rowH * 0.5f - bs.y * 0.5f - 2);
                dl->AddRectFilled(bmin, ImVec2(bmin.x + bs.x + 12, bmin.y + bs.y + 4), IM_COL32(90, 90, 90, 255), 8.0f);
                dl->AddText(ImVec2(bmin.x + 6, bmin.y + 2), IM_COL32(230, 230, 230, 255), badge);
            }
            ImGui::Dummy(ImVec2(w, rowH));
        }
    }
    if (entries.size() != m_ConsoleLastCount && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - rowH * 2)
        ImGui::SetScrollHereY(1.0f);
    m_ConsoleLastCount = entries.size();
    ImGui::EndChild();
    ImGui::End();
}
