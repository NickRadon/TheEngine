#include "editor/Editor.h"

#include "core/Log.h"
#include "core/Platform.h"
#include "core/Project.h"
#include "editor/EditorUI.h"
#include "render/VulkanContext.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <im_anim.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    constexpr const char* kScenesDir = "Assets/Scenes";
    constexpr size_t kMaxUndo = 200;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
bool Editor::Init(VulkanContext* vk, SceneRenderer* renderer, ResourceCache* resources, ScriptEngine* scripts, GLFWwindow* window)
{
    m_Vk = vk;
    m_Renderer = renderer;
    m_Res = resources;
    m_Scripts = scripts;
    m_Window = window;

    // Open the scene the project was last saved with (like Unity), or create one.
    m_ScenePath = Project::GetSetting("lastScene", std::string(kScenesDir) + "/SampleScene.scene");
    if (!fs::exists(m_ScenePath) || !m_Scene.Load(m_ScenePath))
    {
        m_Scene = Scene();
        m_Scene.CreateDefault();
        std::error_code ec;
        fs::create_directories(fs::path(m_ScenePath).parent_path(), ec);
        m_Scene.Save(m_ScenePath);
        LOG_INFO("Created scene %s", m_ScenePath.c_str());
    }
    else
    {
        LOG_INFO("Loaded scene %s", m_ScenePath.c_str());
    }
    m_Scene.name = fs::path(m_ScenePath).stem().string();
    SyncPrefabInstancesAfterLoad();
    m_SceneDirty = false;
    m_Scripts->SetScene(&m_Scene);
    m_Animation.Init(m_Res);
    m_Scripts->SetAnimation(&m_Animation);
    m_Renderer->SetPaletteProvider([this](EntityId id) { return m_Animation.Palette(id); });
    m_Animation.onAnimatorMove = [this](EntityId id) { return m_Scripts->DispatchAnimatorMove(id); };
    m_Animation.onAnimationStream = [this](EntityId id, AnimationStream& stream) { m_Scripts->DispatchAnimatorPose(id, stream); };
    ScanAssets();
    m_Scripts->RequestCompile();

    // Frame the scene nicely on startup.
    m_Camera.SetState(glm::vec3(0.0f, 0.5f, 0.0f), m_Camera.Rotation(), 13.0f, false);

    ImGuizmo::Style& gs = ImGuizmo::GetStyle();
    gs.TranslationLineThickness = 3.0f;
    gs.TranslationLineArrowSize = 7.0f;
    gs.RotationLineThickness = 3.0f;
    gs.RotationOuterLineThickness = 2.5f;
    gs.ScaleLineThickness = 3.0f;
    gs.ScaleLineCircleSize = 6.0f;
    gs.CenterCircleSize = 5.0f;
    gs.Colors[ImGuizmo::DIRECTION_X] = ImGui::ColorConvertU32ToFloat4(EditorUI::AxisColor(0));
    gs.Colors[ImGuizmo::DIRECTION_Y] = ImGui::ColorConvertU32ToFloat4(EditorUI::AxisColor(1));
    gs.Colors[ImGuizmo::DIRECTION_Z] = ImGui::ColorConvertU32ToFloat4(EditorUI::AxisColor(2));
    gs.Colors[ImGuizmo::PLANE_X] = ImGui::ColorConvertU32ToFloat4(EditorUI::AxisColor(0, 0.5f));
    gs.Colors[ImGuizmo::PLANE_Y] = ImGui::ColorConvertU32ToFloat4(EditorUI::AxisColor(1, 0.5f));
    gs.Colors[ImGuizmo::PLANE_Z] = ImGui::ColorConvertU32ToFloat4(EditorUI::AxisColor(2, 0.5f));
    gs.Colors[ImGuizmo::SELECTION] = ImVec4(1.0f, 0.9f, 0.1f, 1.0f);
    ImGuizmo::SetGizmoSizeClipSpace(0.14f);

    LOG_INFO("Welcome to TheEngine. Scene view controls match Unity (RMB+WASD fly, Alt+LMB orbit, MMB pan, F frame).");
    return true;
}

void Editor::Shutdown()
{
    if (m_Playing) ExitPlayMode();
    ClosePrefabMode();
    m_Physics.End();
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void Editor::Update(float dt)
{
    if (m_Test) RunSelfTest();
    if (m_Playtest.enabled) RunPlaytest();
    m_Time += dt;
    m_FpsAccum += dt;
    m_FpsFrames++;
    if (m_FpsAccum >= 0.5f)
    {
        m_Fps = m_FpsFrames / m_FpsAccum;
        m_FpsAccum = 0.0f;
        m_FpsFrames = 0;
    }

    m_FrameStartScene = m_Scene;
    m_FrameStartSelection = m_Selection;

    // Assets: script recompiles, file drops from Explorer, hot reload of textures/materials/models.
    m_Scripts->Update();
    ProcessDrops();
    m_AssetScanTimer += dt;
    if (m_AssetScanTimer > 1.0f)
    {
        m_AssetScanTimer = 0.0f;
        ScanAssets();
        if (m_PrefabModePath.empty()) ScanPrefabs();
        m_Res->CheckForChanges();
    }

    iam_update_begin_frame();
    iam_clip_update(dt);
    ImGuizmo::BeginFrame();

    // Unity tints the editor while in play mode; animate the transition with ImAnim.
    float tint = iam_tween_float(ImHashStr("Editor"), ImHashStr("playTint"), m_Playing ? 1.0f : 0.0f, 0.35f,
                                 iam_ease_preset(iam_ease_in_out_cubic), iam_policy_crossfade, dt, 0.0f);
    EditorUI::ApplyPlayModeTint(tint);

    UpdatePlayMode(dt);
    if (!m_Playing) m_Animation.Update(m_Scene, dt, false); // edit mode: pose preview (default state)
    UpdateCursor();
    HandleShortcuts();

    DrawMainMenu();
    DrawMainToolbar();
    DrawStatusBar();
    DrawDockspace();

    DrawHierarchy();
    DrawSceneView(dt);
    DrawGameView();
    DrawInspector();
    DrawLighting();
    DrawProject();
    DrawConsole();
    DrawAnimator();
    DrawBlendMaskWindow();
    DrawRigWindow();
    DrawToasts(dt);
    DrawAboutPopup();
    if (m_ShowDemo) ImGui::ShowDemoWindow(&m_ShowDemo);

    if (!ImGui::IsAnyItemActive() && !ImGuizmo::IsUsingAny())
        m_EditArmed = false;

    // Selection may reference deleted entities after undo/scene changes.
    m_Selection.erase(std::remove_if(m_Selection.begin(), m_Selection.end(),
                                     [&](EntityId id) { return !m_Scene.Find(id); }), m_Selection.end());

    static float gcTimer = 0.0f;
    gcTimer += dt;
    if (gcTimer > 1.0f)
    {
        iam_gc(600);
        gcTimer = 0.0f;
    }

    char title[256];
    std::snprintf(title, sizeof(title), "%s - %s%s - Windows - <Vulkan>", Project::Name().c_str(), m_Scene.name.c_str(),
                  m_SceneDirty ? "*" : "");
    static std::string lastTitle;
    if (lastTitle != title)
    {
        glfwSetWindowTitle(m_Window, title);
        lastTitle = title;
    }
}

const Entity* Editor::MainCamera() const
{
    for (const Entity& e : m_Scene.entities)
        if (e.camera.enabled && m_Scene.IsActiveInHierarchy(e.id)) return &e;
    return nullptr;
}

RenderView Editor::MakeCameraView(const Entity& cam, float aspect, float time) const
{
    glm::mat4 world = m_Scene.WorldMatrix(cam.id);
    glm::vec3 scale, pos, skew;
    glm::vec4 persp;
    glm::quat rot;
    glm::decompose(world, scale, rot, pos, skew, persp);

    RenderView rv;
    rv.view = glm::inverse(glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(glm::normalize(rot)));
    const CameraComponent& c = cam.camera;
    if (c.orthographic)
        rv.proj = glm::orthoRH_ZO(-c.orthoSize * aspect, c.orthoSize * aspect, -c.orthoSize, c.orthoSize, c.nearClip, c.farClip);
    else
        rv.proj = glm::perspectiveRH_ZO(glm::radians(c.fov), aspect, c.nearClip, c.farClip);
    rv.cameraPos = pos;
    rv.orthographic = c.orthographic;
    rv.nearClip = c.nearClip;
    rv.farClip = c.farClip;
    rv.time = time;
    return rv;
}

void Editor::RenderViews(VkCommandBuffer cmd)
{
    m_Renderer->ResetStats();
    const float time = m_Playing ? m_PlayTime : m_Time;
    m_Renderer->UpdateEnvironment(cmd, m_Scene, time);
    UpdateReflectionProbes(cmd, time);

    if (m_SceneViewVisible && m_Renderer->HasTarget(SceneRenderer::SceneViewId))
    {
        RenderView rv;
        rv.view = m_ViewMatrix;
        rv.proj = m_ProjMatrix;
        rv.cameraPos = m_Camera.Position();
        rv.orthographic = m_Camera.OrthoBlend() > 0.5f;
        rv.drawGrid = m_ShowGrid;
        rv.gridPlane = m_Camera.GridPlane();
        rv.gridOpacity = m_GridOpacity;
        rv.shading = m_Shading;
        rv.drawSky = m_ShowSkybox;
        rv.postProcessing = m_ShowSkybox; // Unity's scene view effects toggle covers skybox and post-processing
        rv.drawOutline = !m_Selection.empty();
        rv.selection = m_Selection;
        rv.time = time;
        m_Camera.ClipRange(rv.nearClip, rv.farClip);
        m_Renderer->Render(cmd, SceneRenderer::SceneViewId, m_Scene, rv);
    }

    if (m_GameViewVisible && m_Renderer->HasTarget(SceneRenderer::GameViewId))
    {
        if (const Entity* cam = MainCamera())
        {
            RenderView rv = MakeCameraView(*cam, m_GameViewSize.x / std::max(m_GameViewSize.y, 1.0f), time);
            m_Renderer->Render(cmd, SceneRenderer::GameViewId, m_Scene, rv);
        }
    }

    if (m_PreviewCamera != kNullEntity && m_Renderer->HasTarget(SceneRenderer::PreviewViewId))
    {
        if (const Entity* cam = m_Scene.Find(m_PreviewCamera))
        {
            RenderView rv = MakeCameraView(*cam, 16.0f / 9.0f, time);
            m_Renderer->Render(cmd, SceneRenderer::PreviewViewId, m_Scene, rv);
        }
    }
}

// Re-bakes (one per frame) reflection probes that moved, changed or whose lighting changed, like Unity's
// automatic probe updates in the editor.
void Editor::UpdateReflectionProbes(VkCommandBuffer cmd, float time)
{
    if (ImGuizmo::IsUsing()) return; // wait until the drag ends
    for (const Entity& e : m_Scene.entities)
    {
        if (!e.reflectionProbe.enabled || !m_Scene.IsActiveInHierarchy(e.id)) continue;
        const glm::mat4 world = m_Scene.WorldMatrix(e.id);
        char sig[256];
        std::snprintf(sig, sizeof(sig), "%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %u", world[3].x, world[3].y, world[3].z,
                      e.reflectionProbe.size.x * glm::length(glm::vec3(world[0])), e.reflectionProbe.size.y * glm::length(glm::vec3(world[1])),
                      e.reflectionProbe.size.z * glm::length(glm::vec3(world[2])), e.reflectionProbe.intensity,
                      e.reflectionProbe.boxProjection ? 1.0f : 0.0f, 0.0f, m_Renderer->LightingVersion());
        auto it = m_ProbeSignatures.find(e.id);
        if (it != m_ProbeSignatures.end() && it->second == sig && m_Renderer->IsProbeBaked(e.id)) continue;
        if (m_Renderer->BakeProbe(cmd, m_Scene, e.id, time)) m_ProbeSignatures[e.id] = sig;
        else m_ProbeSignatures[e.id] = sig; // out of slots: don't retry every frame
        return;
    }
}

void Editor::ResetReflectionProbes()
{
    m_ProbeSignatures.clear();
    m_Renderer->ResetProbes();
}

// ---------------------------------------------------------------------------
// Main menu / toolbar / status bar / docking
// ---------------------------------------------------------------------------
void Editor::DrawMainMenu()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
        if (ImGui::BeginMenu("Open Scene"))
        {
            bool any = false;
            std::error_code ec;
            if (fs::exists(kScenesDir, ec))
                for (const auto& entry : fs::directory_iterator(kScenesDir, ec))
                    if (entry.path().extension() == ".scene")
                    {
                        any = true;
                        if (ImGui::MenuItem(entry.path().stem().string().c_str()))
                            OpenScene(entry.path().string());
                    }
            if (!any) ImGui::TextDisabled("No scenes in %s", kScenesDir);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !m_Playing)) SaveScene();
        ImGui::Separator();
        if (ImGui::MenuItem("New Project..."))
        {
            Platform::LaunchSelf({});
            m_WantsQuit = true;
        }
        if (ImGui::MenuItem("Open Project..."))
        {
            Platform::LaunchSelf({});
            m_WantsQuit = true;
        }
        if (ImGui::MenuItem("Show Project in Explorer")) Platform::RevealInExplorer("Assets");
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) m_WantsQuit = true;
        ImGui::EndMenu();
    }

    // Controller (Animator window) edits have their own history while that window has focus.
    const bool animUndo = m_AnimFocused && AnimUndoAvailable();
    const bool animRedo = m_AnimFocused && AnimRedoAvailable();
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem(animUndo ? "Undo Controller" : "Undo", "Ctrl+Z", false, animUndo || !m_UndoStack.empty()))
        { if (animUndo) AnimUndo(); else Undo(); }
        if (ImGui::MenuItem(animRedo ? "Redo Controller" : "Redo", "Ctrl+Y", false, animRedo || !m_RedoStack.empty()))
        { if (animRedo) AnimRedo(); else Redo(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A")) SelectAll();
        if (ImGui::MenuItem("Deselect All", "Shift+D")) ClearSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !m_Selection.empty())) DuplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, !m_Selection.empty())) DeleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Frame Selected", "F", false, !m_Selection.empty())) FrameSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Play", "Ctrl+P", m_Playing)) m_Playing ? ExitPlayMode() : EnterPlayMode();
        if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", m_Paused)) m_Paused = !m_Paused;
        if (ImGui::MenuItem("Step", "Ctrl+Alt+P", false, m_Playing)) { m_Paused = true; m_StepRequested = true; }
        ImGui::Separator();
        if (ImGui::BeginMenu("Grid and Snap Settings"))
        {
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat("Move", &m_SnapMove, 0.01f, 0.001f, 100.0f, "%.3g");
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat("Rotate", &m_SnapRotate, 0.5f, 0.1f, 180.0f, "%.3g");
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat("Scale", &m_SnapScale, 0.01f, 0.001f, 10.0f, "%.3g");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Preferences"))
        {
            bool vsync = m_Vk->VSync();
            if (ImGui::MenuItem("VSync", nullptr, &vsync)) m_Vk->SetVSync(vsync);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Assets"))
    {
        ProjectContextMenu(m_ProjectFolder);
        ImGui::Separator();
        if (ImGui::MenuItem("Recompile Scripts", nullptr, false, m_Scripts->Available())) m_Scripts->RequestCompile();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject"))
    {
        EntityMenuItems(kNullEntity);
        ImGui::Separator();
        const bool has = !m_Selection.empty();
        if (ImGui::MenuItem("Move To View", "Ctrl+Alt+F", false, has)) MoveSelectionToView();
        if (ImGui::MenuItem("Align With View", "Ctrl+Shift+F", false, has))
        {
            PushUndo();
            for (EntityId id : SelectionRoots())
            {
                glm::mat4 world = glm::translate(glm::mat4(1.0f), m_Camera.Position()) * glm::mat4_cast(m_Camera.Rotation());
                m_Scene.SetWorldMatrix(id, world);
            }
        }
        if (ImGui::MenuItem("Align View to Selected", nullptr, false, has)) AlignViewToSelection();
        if (ImGui::MenuItem("Toggle Active State", "Alt+Shift+A", false, has))
        {
            PushUndo();
            for (EntityId id : m_Selection)
                if (Entity* e = m_Scene.Find(id)) e->active = !e->active;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Component"))
    {
        Entity* e = m_Scene.Find(ActiveEntity());
        auto item = [&](const char* name, bool* flag) {
            if (ImGui::MenuItem(name, nullptr, false, e && !*flag)) { PushUndo(); *flag = true; }
        };
        if (e)
        {
            item("Mesh Renderer", &e->meshRenderer.enabled);
            item("Light", &e->light.enabled);
            item("Camera", &e->camera.enabled);
            if (ImGui::BeginMenu("Scripts"))
            {
                for (const ScriptClassInfo& c : m_Scripts->Classes())
                    if (ImGui::MenuItem(c.name.c_str()))
                    {
                        PushUndo();
                        ScriptComponent sc;
                        sc.className = c.name;
                        for (const ScriptFieldInfo& f : c.fields) sc.fields.push_back({ f.name, f.type, f.defaultValue });
                        e->scripts.push_back(sc);
                    }
                if (m_Scripts->Classes().empty()) ImGui::TextDisabled("No compiled scripts");
                ImGui::EndMenu();
            }
        }
        else
        {
            ImGui::TextDisabled("Select a GameObject");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window"))
    {
        if (ImGui::BeginMenu("Layouts"))
        {
            if (ImGui::MenuItem("Default")) m_ResetLayout = true;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("General"))
        {
            for (const char* name : { "Scene", "Game", "Inspector", "Hierarchy", "Project", "Console" })
                if (ImGui::MenuItem(name)) ImGui::SetWindowFocus(name);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Animation"))
        {
            if (ImGui::MenuItem("Animator")) m_FocusAnimator = true;
            if (ImGui::MenuItem("Blend Mask", nullptr, false, !m_MaskPath.empty())) m_FocusMask = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rendering"))
        {
            if (ImGui::MenuItem("Lighting")) ImGui::SetWindowFocus("Lighting");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_ShowDemo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About TheEngine / Controls")) m_OpenAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Editor::DrawMainToolbar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = 36.0f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.098f, 0.098f, 0.098f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::BeginViewportSideBar("##MainToolbar", vp, ImGuiDir_Up, height, flags))
    {
        // Left: engine label
        ImGui::AlignTextToFramePadding();
        EditorUI::DrawIcon(ImGui::GetWindowDrawList(), EditorUI::Icon::Cube,
                           ImVec2(ImGui::GetCursorScreenPos().x + 9, ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight() * 0.5f),
                           16.0f, IM_COL32(210, 210, 210, 255));
        ImGui::Dummy(ImVec2(20, 0));
        ImGui::SameLine();
        ImGui::TextDisabled("TheEngine");

        // Center: play / pause / step
        const float btn = 30.0f;
        const float groupW = btn * 3 + 4;
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - groupW * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
        if (EditorUI::IconButton("##play", EditorUI::Icon::Play, m_Playing, "Play (Ctrl+P)", ImVec2(btn, 24)))
            m_Playing ? ExitPlayMode() : EnterPlayMode();
        ImGui::SameLine();
        if (EditorUI::IconButton("##pause", EditorUI::Icon::Pause, m_Paused, "Pause (Ctrl+Shift+P)", ImVec2(btn, 24)))
            m_Paused = !m_Paused;
        ImGui::SameLine();
        if (EditorUI::IconButton("##step", EditorUI::Icon::Step, false, "Step (Ctrl+Alt+P)", ImVec2(btn, 24)))
        {
            if (m_Playing) { m_Paused = true; m_StepRequested = true; }
        }
        ImGui::PopStyleVar();

        // Right: layout dropdown
        const float comboW = 110.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - comboW - 8.0f);
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##layout", "Layout"))
        {
            if (ImGui::Selectable("Default")) m_ResetLayout = true;
            ImGui::EndCombo();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void Editor::DrawStatusBar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.098f, 0.098f, 0.098f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::BeginViewportSideBar("##StatusBar", vp, ImGuiDir_Down, ImGui::GetFrameHeight(), flags))
    {
        const auto& entries = Log::Entries();
        if (!entries.empty())
        {
            const LogEntry& last = entries.back();
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetFrameHeight() - 4.0f;
            EditorUI::Icon icon = last.level == LogLevel::Error ? EditorUI::Icon::Error
                                : last.level == LogLevel::Warning ? EditorUI::Icon::Warning : EditorUI::Icon::Info;
            ImU32 col = last.level == LogLevel::Error ? IM_COL32(230, 80, 70, 255)
                      : last.level == LogLevel::Warning ? IM_COL32(240, 190, 60, 255) : IM_COL32(200, 200, 200, 255);
            EditorUI::DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(p.x + 7, p.y + h * 0.5f + 1), 13.0f, col);
            ImGui::Dummy(ImVec2(16, 0));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(last.message.c_str());
            if (ImGui::IsItemClicked()) ImGui::SetWindowFocus("Console");
        }
        char right[200];
        std::snprintf(right, sizeof(right), "%.0f FPS  |  %u draws  |  %s", m_Fps, m_Renderer->DrawCalls(), m_Vk->DeviceName());
        const float rightW = ImGui::CalcTextSize(right).x + 12.0f;
        if (m_Scripts->IsCompiling())
        {
            // Unity-style spinner while scripts compile.
            const char* label = "Compiling scripts...";
            const float w = ImGui::CalcTextSize(label).x + 30.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - rightW - w - 20.0f);
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float r = 6.0f, a = m_Time * 6.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 c(p.x + 8, p.y + ImGui::GetFrameHeight() * 0.5f - 1);
            dl->PathArcTo(c, r, a, a + IM_PI * 1.4f, 16);
            dl->PathStroke(IM_COL32(128, 203, 255, 255), 0, 2.0f);
            ImGui::Dummy(ImVec2(18, 0));
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        }
        else if (m_Scripts->HasCompileErrors())
        {
            const char* label = "Script compile errors";
            ImGui::SameLine(ImGui::GetWindowWidth() - rightW - ImGui::CalcTextSize(label).x - 20.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", label);
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - rightW);
        ImGui::TextDisabled("%s", right);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void Editor::DrawDockspace()
{
    const ImGuiID dockId = ImHashStr("TheEngineDockSpace");
    const bool needsLayout = m_ResetLayout || ImGui::DockBuilderGetNode(dockId) == nullptr;
    ImGui::DockSpaceOverViewport(dockId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    if (needsLayout)
    {
        BuildDefaultLayout(dockId);
        m_ResetLayout = false;
    }
}

void Editor::BuildDefaultLayout(ImGuiID dockspace)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, vp->WorkSize);

    ImGuiID center = dockspace;
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.23f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("Animator", center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Lighting", right);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockspace);

    m_FocusSceneView = true;
    ImGui::SetWindowFocus("Inspector");
    ImGui::SetWindowFocus("Project");
}

void Editor::DrawAboutPopup()
{
    if (m_OpenAbout)
    {
        ImGui::OpenPopup("About TheEngine");
        m_OpenAbout = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("About TheEngine", nullptr, ImGuiWindowFlags_NoResize))
    {
        if (EditorUI::BoldFont()) ImGui::PushFont(EditorUI::BoldFont(), 0.0f);
        ImGui::TextUnformatted("TheEngine 0.1");
        if (EditorUI::BoldFont()) ImGui::PopFont();
        ImGui::TextDisabled("Vulkan 1.3 + Dear ImGui (docking) + ImGuizmo + ImAnim");
        ImGui::Separator();
        if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            const char* rows[][2] = {
                { "RMB drag", "Look around (flythrough)" },
                { "RMB + W/A/S/D", "Fly forward/left/back/right" },
                { "RMB + Q/E", "Fly down/up" },
                { "RMB + Shift", "Fly faster" },
                { "RMB + Wheel", "Change flythrough speed" },
                { "Alt + LMB drag", "Orbit around pivot" },
                { "MMB drag / Alt + MMB", "Pan" },
                { "Alt + RMB drag / Wheel", "Zoom" },
                { "Arrow keys", "Move on the ground plane" },
                { "F / double-click", "Frame selected" },
                { "Q W E R T Y", "View / Move / Rotate / Scale / Rect / Transform tool" },
                { "Z / X", "Toggle Pivot-Center / Global-Local" },
                { "Ctrl while dragging", "Snap (Edit > Grid and Snap Settings)" },
                { "LMB drag on empty", "Marquee selection (Ctrl/Shift add)" },
                { "Scene gizmo", "Click axes to snap view, label toggles Persp/Iso" },
                { "Ctrl+D / Del / F2", "Duplicate / Delete / Rename" },
                { "Ctrl+Z / Ctrl+Y", "Undo / Redo" },
                { "Ctrl+Alt+F / Ctrl+Shift+F", "Move to view / Align with view" },
                { "Ctrl+P", "Play / Stop" },
            };
            for (auto& r : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r[0]);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r[1]);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::DrawToasts(float dt)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - 12.0f;
    const float lifetime = 2.6f;
    for (int i = static_cast<int>(m_Toasts.size()) - 1; i >= 0; --i)
    {
        Toast& t = m_Toasts[i];
        t.age += dt;
        const bool visible = t.age < lifetime - 0.3f;
        float alpha = iam_tween_float(t.id, ImHashStr("alpha"), visible ? 1.0f : 0.0f, 0.3f,
                                      iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt, 0.0f);
        float slide = iam_tween_float(t.id, ImHashStr("slide"), visible ? 0.0f : 30.0f, 0.35f,
                                      iam_ease_spring_desc(1.0f, 180.0f, 16.0f, 0.0f), iam_policy_crossfade, dt, 40.0f);
        ImVec2 size = ImGui::CalcTextSize(t.text.c_str());
        size.x += 36.0f;
        size.y += 14.0f;
        ImVec2 max(vp->WorkPos.x + vp->WorkSize.x - 14.0f + slide, y);
        ImVec2 min(max.x - size.x, max.y - size.y);
        dl->AddRectFilled(min, max, IM_COL32(40, 40, 40, static_cast<int>(235 * alpha)), 4.0f);
        dl->AddRect(min, max, IM_COL32(58, 121, 187, static_cast<int>(255 * alpha)), 4.0f);
        EditorUI::DrawIcon(dl, EditorUI::Icon::Info, ImVec2(min.x + 14.0f, (min.y + max.y) * 0.5f), 14.0f,
                           IM_COL32(128, 203, 255, static_cast<int>(255 * alpha)));
        dl->AddText(ImVec2(min.x + 26.0f, min.y + 7.0f), IM_COL32(220, 220, 220, static_cast<int>(255 * alpha)), t.text.c_str());
        y -= (size.y + 6.0f) * alpha;
        if (t.age > lifetime) m_Toasts.erase(m_Toasts.begin() + i);
    }
}

void Editor::Notify(const std::string& text)
{
    m_Toasts.push_back({ text, 0.0f, ImHashStr("toast") + m_ToastCounter++ });
    LOG_INFO("%s", text.c_str());
}

// ---------------------------------------------------------------------------
// Selection & actions
// ---------------------------------------------------------------------------
bool Editor::IsSelected(EntityId id) const
{
    return std::find(m_Selection.begin(), m_Selection.end(), id) != m_Selection.end();
}

void Editor::Select(EntityId id, bool additive)
{
    if (id == kNullEntity)
    {
        if (!additive) m_Selection.clear();
        return;
    }
    if (additive)
    {
        auto it = std::find(m_Selection.begin(), m_Selection.end(), id);
        if (it != m_Selection.end()) m_Selection.erase(it);
        else m_Selection.push_back(id);
    }
    else
    {
        m_Selection.assign(1, id);
    }
    m_LastClicked = id;
}

std::vector<EntityId> Editor::SelectionRoots() const
{
    std::vector<EntityId> roots;
    for (EntityId id : m_Selection)
    {
        bool hasSelectedAncestor = false;
        for (EntityId other : m_Selection)
            if (other != id && m_Scene.IsAncestor(other, id)) { hasSelectedAncestor = true; break; }
        if (!hasSelectedAncestor) roots.push_back(id);
    }
    return roots;
}

void Editor::SelectAll()
{
    m_Selection.clear();
    for (const Entity& e : m_Scene.entities) m_Selection.push_back(e.id);
}

EntityId Editor::CreatePrimitive(PrimitiveType type, EntityId parent)
{
    PushUndo();
    Entity& e = m_Scene.Create(UniqueName(m_Scene, PrimitiveName(type)), parent);
    e.meshRenderer.enabled = true;
    e.meshRenderer.mesh = PrimitiveName(type);
    AddDefaultCollider(e);
    EntityId id = e.id;
    PlaceInFrontOfCamera(id);
    Select(id);
    m_ScrollToEntity = id;
    return id;
}

void Editor::FitCollider(Entity& e, ColliderShape shape)
{
    ColliderComponent& c = e.collider;
    c.shape = shape;
    glm::vec3 bmin(-0.5f), bmax(0.5f);
    if (e.meshRenderer.enabled)
        if (const Mesh* mesh = m_Res->GetMesh(e.meshRenderer.mesh))
        {
            bmin = mesh->data.boundsMin;
            bmax = mesh->data.boundsMax;
        }
    const glm::vec3 size = bmax - bmin;
    c.center = (bmin + bmax) * 0.5f;
    c.size = glm::max(size, glm::vec3(0.001f));
    if (shape == ColliderShape::Sphere) c.radius = std::max({ size.x, size.y, size.z }) * 0.5f;
    if (shape == ColliderShape::Capsule)
    {
        c.radius = std::max(size.x, size.z) * 0.5f;
        c.height = size.y;
    }
    if (shape == ColliderShape::Mesh) c.center = glm::vec3(0.0f);
    MarkEdited();
}

EntityId Editor::CreateEntity(const char* kind, EntityId parent)
{
    const std::string k = kind;
    for (int i = 1; i < static_cast<int>(PrimitiveType::Count); ++i)
        if (k == PrimitiveName(static_cast<PrimitiveType>(i)))
            return CreatePrimitive(static_cast<PrimitiveType>(i), parent);

    PushUndo();
    Entity& e = m_Scene.Create(UniqueName(m_Scene, k == "Empty" ? "GameObject" : k), parent);
    if (k == "Directional Light")
    {
        e.light.enabled = true;
        e.transform.SetEuler({ -50.0f, -30.0f, 0.0f });
    }
    else if (k == "Point Light")
    {
        e.light.enabled = true;
        e.light.type = LightType::Point;
        e.light.color = glm::vec3(1.0f);
    }
    else if (k == "Spot Light")
    {
        e.light.enabled = true;
        e.light.type = LightType::Spot;
        e.light.color = glm::vec3(1.0f);
        e.light.intensity = 3.0f;
        e.transform.SetEuler({ -90.0f, 0.0f, 0.0f });
    }
    else if (k == "Camera")
    {
        e.camera.enabled = true;
    }
    else if (k == "Reflection Probe")
    {
        e.reflectionProbe.enabled = true;
    }
    else if (k == "Global Volume" || k == "Box Volume")
    {
        e.volume.enabled = true;
        e.volume.isGlobal = k == "Global Volume";
        e.volume.settings.bloom = true;
        e.volume.settings.tonemapping = true;
    }
    EntityId id = e.id;
    PlaceInFrontOfCamera(id);
    Select(id);
    m_ScrollToEntity = id;
    return id;
}

void Editor::PlaceInFrontOfCamera(EntityId id)
{
    Entity* e = m_Scene.Find(id);
    if (!e) return;
    if (e->parent != kNullEntity)
    {
        e->transform.position = glm::vec3(0.0f); // children are created at the parent's origin, like Unity
        return;
    }
    glm::mat4 world = m_Scene.WorldMatrix(id);
    world[3] = glm::vec4(m_Camera.Pivot(), 1.0f);
    m_Scene.SetWorldMatrix(id, world);
}

void Editor::DeleteSelection()
{
    if (m_Selection.empty()) return;
    PushUndo();
    for (EntityId id : SelectionRoots()) m_Scene.Destroy(id);
    m_Selection.clear();
}

void Editor::DuplicateSelection()
{
    if (m_Selection.empty()) return;
    PushUndo();
    std::vector<EntityId> created;
    for (EntityId id : SelectionRoots())
    {
        EntityId copy = m_Scene.Duplicate(id);
        if (copy) created.push_back(copy);
    }
    m_Selection = created;
}

bool Editor::EntityBounds(EntityId id, glm::vec3& center, float& radius) const
{
    const Entity* e = m_Scene.Find(id);
    if (!e) return false;
    glm::mat4 world = m_Scene.WorldMatrix(id);
    const Mesh* mesh = e->meshRenderer.enabled ? m_Res->GetMesh(e->meshRenderer.mesh) : nullptr;
    if (mesh)
    {
        const MeshData& md = mesh->data;
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 corner((i & 1) ? md.boundsMax.x : md.boundsMin.x, (i & 2) ? md.boundsMax.y : md.boundsMin.y,
                             (i & 4) ? md.boundsMax.z : md.boundsMin.z);
            glm::vec3 w = glm::vec3(world * glm::vec4(corner, 1.0f));
            mn = glm::min(mn, w);
            mx = glm::max(mx, w);
        }
        center = (mn + mx) * 0.5f;
        radius = glm::length(mx - mn) * 0.5f;
    }
    else
    {
        center = glm::vec3(world[3]);
        radius = 1.0f;
    }
    return true;
}

void Editor::FrameSelection()
{
    if (m_Selection.empty()) return;
    glm::vec3 mn(1e30f), mx(-1e30f);
    bool any = false;
    auto accumulate = [&](EntityId id) {
        glm::vec3 c;
        float r;
        if (!EntityBounds(id, c, r)) return;
        mn = glm::min(mn, c - glm::vec3(r));
        mx = glm::max(mx, c + glm::vec3(r));
        any = true;
    };
    for (EntityId id : m_Selection)
    {
        accumulate(id);
        for (const Entity& e : m_Scene.entities)
            if (m_Scene.IsAncestor(id, e.id) && e.meshRenderer.enabled) accumulate(e.id);
    }
    if (!any) return;
    glm::vec3 center = (mn + mx) * 0.5f;
    float radius = glm::length(mx - mn) * 0.5f / 1.7320508f; // undo the cube inflation of spheres
    m_Camera.Frame(center, std::max(radius, 0.25f));
}

void Editor::MoveSelectionToView()
{
    if (m_Selection.empty()) return;
    PushUndo();
    glm::vec3 c(0.0f);
    auto roots = SelectionRoots();
    for (EntityId id : roots) c += glm::vec3(m_Scene.WorldMatrix(id)[3]);
    c /= static_cast<float>(roots.size());
    glm::vec3 offset = m_Camera.Pivot() - c;
    for (EntityId id : roots)
    {
        glm::mat4 w = m_Scene.WorldMatrix(id);
        w[3] += glm::vec4(offset, 0.0f);
        m_Scene.SetWorldMatrix(id, w);
    }
}

void Editor::AlignViewToSelection()
{
    EntityId id = ActiveEntity();
    if (!id) return;
    glm::mat4 world = m_Scene.WorldMatrix(id);
    glm::vec3 scale, pos, skew;
    glm::vec4 persp;
    glm::quat rot;
    glm::decompose(world, scale, rot, pos, skew, persp);
    m_Camera.AlignWith(pos, glm::normalize(rot));
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------
void Editor::PushUndo()
{
    m_UndoStack.push_back({ m_Scene, m_Selection });
    if (m_UndoStack.size() > kMaxUndo) m_UndoStack.pop_front();
    m_RedoStack.clear();
    m_SceneDirty = true;
}

void Editor::MarkEdited()
{
    if (!m_EditArmed)
    {
        m_UndoStack.push_back({ m_FrameStartScene, m_FrameStartSelection });
        if (m_UndoStack.size() > kMaxUndo) m_UndoStack.pop_front();
        m_RedoStack.clear();
        m_EditArmed = true;
    }
    m_SceneDirty = true;
}

void Editor::Undo()
{
    if (m_UndoStack.empty()) return;
    m_RedoStack.push_back({ m_Scene, m_Selection });
    m_Scene = std::move(m_UndoStack.back().scene);
    m_Selection = std::move(m_UndoStack.back().selection);
    m_UndoStack.pop_back();
    m_EditArmed = false;
    m_SceneDirty = true;
}

void Editor::Redo()
{
    if (m_RedoStack.empty()) return;
    m_UndoStack.push_back({ m_Scene, m_Selection });
    m_Scene = std::move(m_RedoStack.back().scene);
    m_Selection = std::move(m_RedoStack.back().selection);
    m_RedoStack.pop_back();
    m_EditArmed = false;
    m_SceneDirty = true;
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------
void Editor::EnterPlayMode()
{
    if (m_Playing) return;
    ClosePrefabMode();
    if (m_Scripts->IsCompiling())
    {
        Notify("Waiting for script compilation to finish...");
        return;
    }
    if (m_Scripts->HasCompileErrors())
    {
        LOG_ERROR("All compiler errors have to be fixed before you can enter play mode!");
        ImGui::SetWindowFocus("Console");
        return;
    }
    m_EditModeScene = m_Scene;
    m_Playing = true;
    m_StepRequested = false;
    m_PlayTime = 0.0f;
    m_PlayFrame = 0;
    m_FocusGameView = true;
    LOG_INFO("Entered play mode");
    // Physics first, so Awake/Start can already use rigidbodies.
    m_Physics.Begin(&m_Scene, [this](const std::string& ref) -> const MeshData* {
        const Mesh* mesh = m_Res->GetMesh(ref);
        return mesh ? &mesh->data : nullptr;
    });
    m_Scripts->SetPhysics(&m_Physics);
    m_Animation.Reset();
    m_Scripts->BeginPlay(&m_Scene);
}

void Editor::ExitPlayMode()
{
    if (!m_Playing) return;
    m_Scripts->EndPlay();
    m_Physics.End();
    m_Animation.Reset();
    m_Scene = m_EditModeScene; // changes made in play mode are discarded, like Unity
    m_Playing = false;
    m_Paused = false;
    m_FocusSceneView = true;
    LOG_INFO("Exited play mode");
}

void Editor::UpdatePlayMode(float dt)
{
    if (!m_Playing) return;
    GatherScriptInput();
    if (m_Paused && !m_StepRequested) return;
    if (m_StepRequested) dt = 1.0f / 60.0f;
    m_StepRequested = false;
    m_PlayTime += dt;
    // Unity order: FixedUpdate + physics steps (with collision messages), then Update/LateUpdate.
    m_Physics.Update(dt, [this] { m_Scripts->FixedTick(m_Physics.fixedDeltaTime); },
                     [this](const std::vector<CollisionEvent>& events) { m_Scripts->DispatchCollisions(events); });
    m_Scripts->Tick(std::min(dt, 0.1f), m_PlayTime, m_PlayFrame++);
    // Unity order: Update -> animation (root motion) -> LateUpdate (cameras follow the animated result).
    m_Animation.Update(m_Scene, std::min(dt, 0.1f), true);
    m_Scripts->LateTick();
}

// Maps Unity KeyCode values (used by TheEngine.KeyCode) to ImGui keys. Input only reaches scripts
// while the Game view has focus, like Unity.
void Editor::GatherScriptInput()
{
    ScriptInput& in = m_Scripts->Input();
    const bool active = m_GameViewFocused;
    static const std::pair<int, ImGuiKey> keys[] = {
        { 8, ImGuiKey_Backspace }, { 9, ImGuiKey_Tab }, { 13, ImGuiKey_Enter }, { 27, ImGuiKey_Escape }, { 32, ImGuiKey_Space },
        { 48, ImGuiKey_0 }, { 49, ImGuiKey_1 }, { 50, ImGuiKey_2 }, { 51, ImGuiKey_3 }, { 52, ImGuiKey_4 },
        { 53, ImGuiKey_5 }, { 54, ImGuiKey_6 }, { 55, ImGuiKey_7 }, { 56, ImGuiKey_8 }, { 57, ImGuiKey_9 },
        { 127, ImGuiKey_Delete }, { 273, ImGuiKey_UpArrow }, { 274, ImGuiKey_DownArrow }, { 275, ImGuiKey_RightArrow },
        { 276, ImGuiKey_LeftArrow }, { 303, ImGuiKey_RightShift }, { 304, ImGuiKey_LeftShift }, { 305, ImGuiKey_RightCtrl },
        { 306, ImGuiKey_LeftCtrl }, { 307, ImGuiKey_RightAlt }, { 308, ImGuiKey_LeftAlt },
    };
    auto set = [&](int code, ImGuiKey key) {
        in.key[code] = active && ImGui::IsKeyDown(key);
        in.keyDown[code] = active && ImGui::IsKeyPressed(key, false);
        in.keyUp[code] = active && ImGui::IsKeyReleased(key);
    };
    for (auto& [code, key] : keys) set(code, key);
    for (int i = 0; i < 26; ++i) set(97 + i, static_cast<ImGuiKey>(ImGuiKey_A + i));
    for (int i = 0; i < 12; ++i) set(282 + i, static_cast<ImGuiKey>(ImGuiKey_F1 + i));
    for (int b = 0; b < 5; ++b)
    {
        in.mouse[b] = active && ImGui::IsMouseDown(b);
        in.mouseDown[b] = active && ImGui::IsMouseClicked(b);
        in.mouseUp[b] = active && ImGui::IsMouseReleased(b);
        in.key[323 + b] = in.mouse[b];
        in.keyDown[323 + b] = in.mouseDown[b];
        in.keyUp[323 + b] = in.mouseUp[b];
    }
    const ImGuiIO& io = ImGui::GetIO();
    in.mouseX = io.MousePos.x - m_GameImageMin.x;
    in.mouseY = m_GameImageSize.y - (io.MousePos.y - m_GameImageMin.y); // bottom-left origin like Unity
    in.mouseDX = active ? io.MouseDelta.x : 0.0f;
    in.mouseDY = active ? io.MouseDelta.y : 0.0f;
    in.wheel = active ? io.MouseWheel : 0.0f;

    // --playtest: keys held for the whole session plus timed taps, with down/up edges like real input.
    if (m_Playtest.enabled && m_Playing)
    {
        std::vector<bool> now(ScriptInput::kKeyCount, false);
        for (int code : m_Playtest.keys)
            if (code >= 0 && code < ScriptInput::kKeyCount) now[code] = true;
        for (auto [code, at] : m_Playtest.presses)
            if (code >= 0 && code < ScriptInput::kKeyCount && m_PlayTime >= at && m_PlayTime < at + 0.1f) now[code] = true;
        m_Playtest.down.resize(ScriptInput::kKeyCount, false);
        for (int code = 0; code < ScriptInput::kKeyCount; ++code)
        {
            const bool was = m_Playtest.down[code];
            if (!now[code] && !was) continue;
            in.key[code] = in.key[code] || now[code];
            in.keyDown[code] = in.keyDown[code] || (now[code] && !was);
            in.keyUp[code] = in.keyUp[code] || (!now[code] && was);
            if (code >= 323 && code < 328)
            {
                in.mouse[code - 323] = in.key[code];
                in.mouseDown[code - 323] = in.keyDown[code];
                in.mouseUp[code - 323] = in.keyUp[code];
            }
            m_Playtest.down[code] = now[code];
        }
        in.mouseDX += m_PlaytestMouse.x * ImGui::GetIO().DeltaTime;
        in.mouseDY += m_PlaytestMouse.y * ImGui::GetIO().DeltaTime;
    }
}

// Cursor.lockState: while playing with the Game view focused the OS cursor is captured (mouse deltas keep coming).
// Escape releases it until the Game view is clicked again, like Unity's editor.
void Editor::UpdateCursor()
{
    const bool wantLock = m_Playing && m_Scripts->cursorLock != 0 && m_GameViewFocused && !m_CursorReleased;
    if (m_Playing && m_CursorLockedByScript && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_CursorReleased = true;
    if (m_CursorReleased && m_GameViewFocused && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_CursorReleased = false;
    if (!m_Playing) m_CursorReleased = false;
    const bool hidden = m_Playing && m_GameViewFocused && !m_Scripts->cursorVisible && !m_CursorReleased;
    const int mode = wantLock ? GLFW_CURSOR_DISABLED : hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL;
    if (mode != m_CursorMode && m_Window)
    {
        glfwSetInputMode(m_Window, GLFW_CURSOR, mode);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, mode == GLFW_CURSOR_DISABLED ? GLFW_TRUE : GLFW_FALSE);
        m_CursorMode = mode;
    }
    m_CursorLockedByScript = wantLock;
}

void Editor::EnablePlaytest(float seconds, const std::vector<int>& heldKeys, const std::vector<std::pair<int, float>>& pressedKeys,
                            const std::string& captureDir)
{
    m_Playtest.presses = pressedKeys;
    m_Playtest.enabled = true;
    m_Playtest.seconds = seconds;
    m_Playtest.keys = heldKeys;
    m_Playtest.captureDir = captureDir;
}

void Editor::RunPlaytest()
{
    Playtest& p = m_Playtest;
    ++p.frames;
    if (!p.started)
    {
        if (p.frames < 30 || m_Scripts->IsCompiling()) return;
        if (m_Scripts->HasCompileErrors())
        {
            LOG_ERROR("[playtest] scripts have compile errors");
            m_WantsQuit = true;
            return;
        }
        // Editor screenshot with the Animator window showing the selected object's controller.
        ++p.readyFrames;
        if (!p.captureDir.empty() && p.readyFrames < 30)
        {
            if (p.readyFrames == 1)
                for (const Entity& e : m_Scene.entities)
                    if (e.animator.enabled) { Select(e.id); break; }
            m_FocusAnimator = true;
            return;
        }
        if (!p.captureDir.empty() && p.readyFrames == 30) { m_Vk->RequestScreenshot(p.captureDir + "/editor_animator.bmp"); return; }
        // Scene view framed on the character from the front-left, for checking poses from outside.
        if (!p.captureDir.empty() && p.readyFrames < 60)
        {
            ImGui::SetWindowFocus("Scene");
            if (const Entity* sel = m_Scene.Find(ActiveEntity()))
            {
                const glm::vec3 target = glm::vec3(m_Scene.WorldMatrix(sel->id)[3]) + glm::vec3(0.0f, 1.3f, 0.0f);
                m_Camera.SetState(target, glm::angleAxis(glm::radians(-150.0f), glm::vec3(0, 1, 0)) *
                                              glm::angleAxis(glm::radians(-10.0f), glm::vec3(1, 0, 0)), 2.2f, false);
            }
            return;
        }
        if (!p.captureDir.empty()) m_Renderer->CaptureView(SceneRenderer::SceneViewId, p.captureDir + "/edit_scene.bmp");
        EnterPlayMode();
        p.started = true;
        p.nextLog = 0.0f;
        return;
    }
    if (!m_Playing)
    {
        m_WantsQuit = true;
        return;
    }
    if (m_PlayTime >= p.nextLog)
    {
        p.nextLog += 0.5f;
        for (const Entity& e : m_Scene.entities)
        {
            if (!e.animator.enabled) continue;
            const glm::vec3 pos = e.transform.position;
            std::string state = "-";
            if (AnimatorInstance* inst = m_Animation.Instance(e.id))
            {
                const int s = inst->NextState() >= 0 ? inst->NextState() : inst->CurrentState();
                if (s >= 0) state = inst->Controller()->Base().states[s].name;
                for (int l = 1; l < static_cast<int>(inst->Controller()->layers.size()); ++l)
                {
                    const int ls = inst->NextState(l) >= 0 ? inst->NextState(l) : inst->CurrentState(l);
                    if (ls >= 0) state += " | " + inst->Controller()->layers[l].name + ": " + inst->Controller()->layers[l].states[ls].name;
                }
            }
            LOG_INFO("[playtest] t=%.2f %s pos (%.3f %.3f %.3f) yaw %.1f state %s fps %.0f", m_PlayTime, e.name.c_str(), pos.x, pos.y, pos.z,
                     e.transform.euler.y, state.c_str(), m_Fps);
            glm::mat4 headBone(1.0f);
            if (m_Animation.BoneModelMatrix(e.id, "head", headBone))
            {
                const glm::vec3 h(headBone[3]), x(headBone[0]), y(headBone[1]), z(headBone[2]);
                LOG_INFO("[playtest] head pos (%.3f %.3f %.3f) axes X(%.3f %.3f %.3f) Y(%.3f %.3f %.3f) Z(%.3f %.3f %.3f)",
                         h.x, h.y, h.z, x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z);
            }
            for (const Entity& camera : m_Scene.entities) if (camera.camera.enabled)
            {
                const glm::mat4 cameraWorld = m_Scene.WorldMatrix(camera.id);
                const glm::vec3 cameraPos(cameraWorld[3]);
                const glm::vec3 forward = glm::normalize(-glm::vec3(cameraWorld[2]));
                LOG_INFO("[playtest] camera pos (%.3f %.3f %.3f) forward (%.3f %.3f %.3f)",
                         cameraPos.x, cameraPos.y, cameraPos.z, forward.x, forward.y, forward.z);
            }
            glm::mat4 weaponBone(1.0f);
            if (m_Animation.BoneModelMatrix(e.id, "vb_ak_weapon", weaponBone))
            {
                const glm::mat4 weaponWorld = m_Scene.WorldMatrix(e.id) * weaponBone;
                const glm::vec3 forward = glm::normalize(-glm::vec3(weaponWorld[2]));
                LOG_INFO("[playtest] weapon forward (%.3f %.3f %.3f)", forward.x, forward.y, forward.z);
                for (const char* side : { "l", "r" })
                {
                    glm::mat4 upper(1.0f), lower(1.0f), hand(1.0f), grip(1.0f);
                    const std::string suffix(side);
                    if (!m_Animation.BoneModelMatrix(e.id, "upperarm_" + suffix, upper) ||
                        !m_Animation.BoneModelMatrix(e.id, "lowerarm_" + suffix, lower) ||
                        !m_Animation.BoneModelMatrix(e.id, "hand_" + suffix, hand) ||
                        !m_Animation.BoneModelMatrix(e.id, "vb_ak_hand_" + suffix, grip)) continue;
                    const glm::vec3 a(upper[3]), b(lower[3]), c(hand[3]), target(grip[3]);
                    const float reach = glm::length(b - a) + glm::length(c - b);
                    const float desired = glm::length(target - a);
                    LOG_INFO("[playtest] %s arm target %.3f m / reach %.3f m, hand error %.3f m",
                             side, desired, reach, glm::length(c - target));
                }
            }
        }
        if (!p.captureDir.empty() && m_Renderer->HasTarget(SceneRenderer::GameViewId) && m_PlayTime > 0.0f)
        {
            char name[64];
            std::snprintf(name, sizeof(name), "/play_%02d.bmp", p.captures++);
            m_Renderer->CaptureView(SceneRenderer::GameViewId, p.captureDir + name);
            if (m_Renderer->HasTarget(SceneRenderer::SceneViewId))
            {
                std::snprintf(name, sizeof(name), "/scene_play_%02d.bmp", p.captures - 1);
                m_Renderer->CaptureView(SceneRenderer::SceneViewId, p.captureDir + name);
            }
        }
        // Live Animator window (state progress) shortly before the end.
        if (!p.captureDir.empty() && m_PlayTime + 0.6f >= p.seconds && m_PlayTime + 0.1f < p.seconds) m_FocusAnimator = true;
    }
    if (!p.captureDir.empty() && m_PlayTime + 0.1f >= p.seconds && m_PlayTime < p.seconds)
        m_Vk->RequestScreenshot(p.captureDir + "/editor_animator_live.bmp");
    if (m_PlayTime >= p.seconds)
    {
        ExitPlayMode();
        m_WantsQuit = true;
    }
}

// ---------------------------------------------------------------------------
// Scenes
// ---------------------------------------------------------------------------
void Editor::NewScene()
{
    if (m_Playing) ExitPlayMode();
    ClosePrefabMode();
    PushUndo();
    m_Scene = Scene();
    ResetReflectionProbes();
    std::string name = "Untitled";
    for (int i = 1; fs::exists(std::string(kScenesDir) + "/" + name + ".scene"); ++i) name = "Untitled " + std::to_string(i);
    m_Scene.name = name;
    m_ScenePath = std::string(kScenesDir) + "/" + name + ".scene";
    Entity& cam = m_Scene.Create("Main Camera");
    cam.camera.enabled = true;
    cam.transform.position = { 0.0f, 1.0f, 10.0f };
    Entity& light = m_Scene.Create("Directional Light");
    light.light.enabled = true;
    light.transform.position = { 0.0f, 3.0f, 0.0f };
    light.transform.SetEuler({ -50.0f, -30.0f, 0.0f });
    m_Selection.clear();
    m_SceneDirty = true;
    m_Camera.SetState(glm::vec3(0.0f), m_Camera.Rotation(), 10.0f, true);
    Notify("New scene created");
}

void Editor::SaveScene()
{
    if (m_Playing)
    {
        LOG_WARN("Cannot save the scene while in play mode");
        return;
    }
    if (!m_PrefabModePath.empty())
    {
        SavePrefabMode();
        return;
    }
    RefreshPrefabOverrides(); // store which properties each prefab instance overrides
    std::error_code ec;
    fs::create_directories(fs::path(m_ScenePath).parent_path(), ec);
    if (m_Scene.Save(m_ScenePath))
    {
        m_SceneDirty = false;
        Project::SetSetting("lastScene", m_ScenePath);
        Notify("Saved " + m_ScenePath);
    }
    else
    {
        LOG_ERROR("Failed to save %s", m_ScenePath.c_str());
    }
}

void Editor::OpenScene(const std::string& path)
{
    if (m_Playing) ExitPlayMode();
    ClosePrefabMode();
    Scene loaded;
    if (!loaded.Load(path))
    {
        LOG_ERROR("Failed to load scene %s", path.c_str());
        return;
    }
    m_Scene = std::move(loaded);
    m_ScenePath = fs::path(path).generic_string();
    m_Scene.name = fs::path(path).stem().string();
    SyncPrefabInstancesAfterLoad();
    ResetReflectionProbes();
    m_Selection.clear();
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_SceneDirty = false;
    Project::SetSetting("lastScene", m_ScenePath);
    Notify("Opened " + m_ScenePath);
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------
bool Editor::TextInputActive() const
{
    return ImGui::GetIO().WantTextInput;
}

void Editor::HandleShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (TextInputActive()) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

    // Global shortcuts
    if (ctrl && pressed(ImGuiKey_Z))
    {
        if (m_AnimFocused && (shift ? AnimRedo() : AnimUndo())) return;
        shift ? Redo() : Undo();
        return;
    }
    if (ctrl && pressed(ImGuiKey_Y))
    {
        if (m_AnimFocused && AnimRedo()) return;
        Redo();
        return;
    }
    if (ctrl && pressed(ImGuiKey_S)) { SaveScene(); return; }
    if (ctrl && pressed(ImGuiKey_N) && !shift) { NewScene(); return; }
    if (ctrl && pressed(ImGuiKey_P))
    {
        if (alt) { if (m_Playing) { m_Paused = true; m_StepRequested = true; } }
        else if (shift) m_Paused = !m_Paused;
        else m_Playing ? ExitPlayMode() : EnterPlayMode();
        return;
    }
    if (ctrl && shift && pressed(ImGuiKey_N)) { CreateEntity("Empty"); return; }
    if (alt && shift && pressed(ImGuiKey_N)) { CreateEntity("Empty", ActiveEntity()); return; }
    if (alt && shift && pressed(ImGuiKey_A))
    {
        PushUndo();
        for (EntityId id : m_Selection)
            if (Entity* e = m_Scene.Find(id)) e->active = !e->active;
        return;
    }

    // While playing with the Game view focused, keys belong to the game (like Unity).
    if (m_Playing && m_GameViewFocused) return;

    // Shortcuts that only apply when the editor (not a popup) has focus and the camera isn't flying.
    if (m_Camera.IsFlying()) return;
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;

    if (ctrl && pressed(ImGuiKey_D)) { DuplicateSelection(); return; }
    if (ctrl && pressed(ImGuiKey_A)) { SelectAll(); return; }
    if (ctrl && alt && pressed(ImGuiKey_F)) { MoveSelectionToView(); return; }
    if (ctrl && shift && pressed(ImGuiKey_F))
    {
        if (m_Selection.empty()) return;
        PushUndo();
        for (EntityId id : SelectionRoots())
            m_Scene.SetWorldMatrix(id, glm::translate(glm::mat4(1.0f), m_Camera.Position()) * glm::mat4_cast(m_Camera.Rotation()));
        return;
    }
    if (pressed(ImGuiKey_Delete)) { DeleteSelection(); return; }
    if (shift && pressed(ImGuiKey_D) && !ctrl) { ClearSelection(); return; }
    if (pressed(ImGuiKey_F2) && ActiveEntity())
    {
        m_RenameEntity = ActiveEntity();
        std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", m_Scene.Find(m_RenameEntity)->name.c_str());
        m_RenameFocus = true;
        return;
    }

    if (ctrl || alt) return;
    const bool sceneContext = m_SceneViewHovered || m_SceneViewFocused;
    if (pressed(ImGuiKey_F) && (sceneContext || ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)))
    {
        FrameSelection();
        return;
    }
    if (pressed(ImGuiKey_Q)) m_Tool = Tool::View;
    if (pressed(ImGuiKey_W)) m_Tool = Tool::Move;
    if (pressed(ImGuiKey_E)) m_Tool = Tool::Rotate;
    if (pressed(ImGuiKey_R)) m_Tool = Tool::Scale;
    if (pressed(ImGuiKey_T)) m_Tool = Tool::Rect;
    if (pressed(ImGuiKey_Y)) m_Tool = Tool::Transform;
    if (pressed(ImGuiKey_Z)) m_PivotMode = !m_PivotMode;
    if (pressed(ImGuiKey_X)) m_LocalSpace = !m_LocalSpace;
}
