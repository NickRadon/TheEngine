#pragma once

#include "editor/EditorCamera.h"
#include "render/Resources.h"
#include "render/SceneRenderer.h"
#include "scene/Prefab.h"
#include "scene/Scene.h"
#include "scripting/ScriptEngine.h"

#include <imgui.h>

#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
class VulkanContext;

// Unity's transform tools (Q W E R T Y).
enum class Tool : int { View = 0, Move, Rotate, Scale, Rect, Transform };

class Editor
{
public:
    Editor();
    ~Editor();
    bool Init(VulkanContext* vk, SceneRenderer* renderer, ResourceCache* resources, ScriptEngine* scripts, GLFWwindow* window);
    // Files dropped onto the window from the OS (imported into the current Project folder).
    void OnFilesDropped(const std::vector<std::string>& paths);
    void Shutdown();

    // Build the whole editor UI for this frame (between ImGui::NewFrame and ImGui::Render).
    void Update(float dt);
    // Record offscreen rendering of the scene / game / camera-preview views.
    void RenderViews(VkCommandBuffer cmd);

    bool WantsQuit() const { return m_WantsQuit; }

    // Automated UI test (--selftest): drives the editor through ImGui input events and exits.
    void EnableSelfTest(const std::string& captureDir = "");
    int SelfTestFailures() const;

private:
    struct SelfTest;
    void RunSelfTest();
    std::unique_ptr<SelfTest> m_Test;

    // --- Panels ---
    void DrawMainMenu();
    void DrawMainToolbar();
    void DrawStatusBar();
    void DrawDockspace();
    void BuildDefaultLayout(ImGuiID dockspace);
    void DrawHierarchy();
    void DrawHierarchyNode(EntityId id, const std::string& filter);
    void DrawInspector();
    void DrawLighting();
    void DrawProject();
    void DrawConsole();
    void DrawSceneView(float dt);
    void DrawGameView();
    void DrawGameContent();
    void DrawToasts(float dt);
    void DrawAboutPopup();

    // --- Scene view helpers ---
    void SceneViewToolbar();
    void SceneViewToolsOverlay(ImVec2 origin);
    void SceneViewGizmo();
    void SceneViewIcons(ImDrawList* dl);
    void SceneViewPicking(bool imageHovered);
    void SceneViewDragDrop();
    void SceneViewCameraPreview(ImDrawList* dl);
    EntityId PickEntity(ImVec2 mouse) const;
    bool WorldToScreen(const glm::vec3& world, ImVec2& out) const;
    void ScreenRay(ImVec2 mouse, glm::vec3& origin, glm::vec3& dir) const;
    bool EntityBounds(EntityId id, glm::vec3& center, float& radius) const;
    void EntityMenuItems(EntityId parent); // "Create" submenu entries (GameObject menu / context menus)

    // --- Assets ---
    void DrawAssetInspector();
    void DrawMaterialEditor(const std::string& path);
    void DrawScriptComponent(Entity& e, size_t index, bool& removed);
    void ProjectContextMenu(const std::string& folder);
    std::string CreateAsset(const std::string& folder, const std::string& kind);
    EntityId InstantiateModel(const std::string& path, EntityId parent, const glm::vec3* position);
    void AssignMaterial(EntityId id, const std::string& materialPath);
    void ScanAssets();
    void ProcessDrops();
    bool AcceptAssetDrop(const char* extension, std::string& outPath); // inside BeginDragDropTarget
    void SelectAsset(const std::string& path);
    void GatherScriptInput();

    // --- Prefabs (EditorPrefabs.cpp) ---
    const Prefab::Contents* PrefabContents(const std::string& path);
    std::vector<EntityId> PrefabInstanceRoots(const std::string& path) const; // empty path = all
    void OnPrefabChanged(const std::string& path, const Prefab::Contents& newContents, EntityId except = kNullEntity);
    void SyncPrefabInstancesAfterLoad();
    void RefreshPrefabOverrides();
    void ScanPrefabs();
    EntityId InstantiatePrefab(const std::string& path, EntityId parent, const glm::vec3* position);
    std::string CreatePrefab(EntityId id, const std::string& folder);
    void ApplyPrefabOverrides(EntityId root);
    void RevertPrefabOverrides(EntityId root);
    void UnpackPrefab(EntityId root);
    void OpenPrefabMode(const std::string& path);
    bool SavePrefabMode();
    void ClosePrefabMode();
    void DrawPrefabModeBar();
    void DrawPrefabInstanceHeader(EntityId id);

    // --- Actions ---
    void Select(EntityId id, bool additive = false);
    void ClearSelection() { m_Selection.clear(); }
    bool IsSelected(EntityId id) const;
    EntityId ActiveEntity() const { return m_Selection.empty() ? kNullEntity : m_Selection.back(); }
    std::vector<EntityId> SelectionRoots() const; // selected entities without a selected ancestor

    EntityId CreateEntity(const char* kind, EntityId parent = kNullEntity);
    void FitCollider(Entity& e, ColliderShape shape); // sizes a collider to the mesh bounds, like Unity
    void DrawColliderGizmo(ImDrawList* dl, const Entity& e);
    EntityId CreatePrimitive(PrimitiveType type, EntityId parent = kNullEntity);
    void PlaceInFrontOfCamera(EntityId id);
    void DeleteSelection();
    void DuplicateSelection();
    void FrameSelection();
    void MoveSelectionToView();
    void AlignViewToSelection();
    void SelectAll();

    // Undo (snapshot based; scenes are small).
    struct Snapshot
    {
        Scene scene;
        std::vector<EntityId> selection;
    };
    void PushUndo();                // snapshot of the current state before a structural change
    void MarkEdited();              // call when an inspector widget changed a value (merges while dragging)
    void Undo();
    void Redo();

    // Play mode
    void EnterPlayMode();
    void ExitPlayMode();
    void UpdatePlayMode(float dt);

    // Scenes
    void NewScene();
    void SaveScene();
    void OpenScene(const std::string& path);
    std::string ScenePath() const { return m_ScenePath; }

    void Notify(const std::string& text);
    void HandleShortcuts();
    bool TextInputActive() const;

    // Camera helpers for rendering
    const Entity* MainCamera() const;
    RenderView MakeCameraView(const Entity& cam, float aspect, float time) const;

    VulkanContext* m_Vk = nullptr;
    SceneRenderer* m_Renderer = nullptr;
    ResourceCache* m_Res = nullptr;
    ScriptEngine* m_Scripts = nullptr;
    std::string m_ScenePath;
    GLFWwindow* m_Window = nullptr;
    bool m_WantsQuit = false;
    bool m_ResetLayout = false;
    bool m_ShowDemo = false;
    bool m_OpenAbout = false;

    Scene m_Scene;
    Scene m_FrameStartScene;
    std::vector<EntityId> m_FrameStartSelection;
    std::vector<EntityId> m_Selection;
    std::deque<Snapshot> m_UndoStack;
    std::vector<Snapshot> m_RedoStack;
    bool m_EditArmed = false;
    bool m_SceneDirty = false;

    // Play mode
    bool m_Playing = false;
    bool m_Paused = false;
    bool m_StepRequested = false;
    Scene m_EditModeScene;
    PhysicsWorld m_Physics;
    float m_PlayTime = 0.0f;
    int m_PlayFrame = 0;
    bool m_GameViewFocused = false;
    ImVec2 m_GameImageMin{ 0, 0 }, m_GameImageSize{ 1, 1 };
    bool m_FocusGameView = false;
    bool m_FocusSceneView = false;

    // Tools
    Tool m_Tool = Tool::Move;
    Tool m_ToolBeforeView = Tool::Move;
    bool m_PivotMode = true;   // true = Pivot, false = Center
    bool m_LocalSpace = false; // true = Local, false = Global
    float m_SnapMove = 0.25f;
    float m_SnapRotate = 15.0f;
    float m_SnapScale = 1.0f;
    bool m_GizmoWasUsing = false;

    // Scene view state
    EditorCamera m_Camera;
    ShadingMode m_Shading = ShadingMode::Shaded;
    bool m_ShowGrid = true;
    bool m_ShowGizmos = true;
    bool m_ShowSkybox = true;
    float m_GridOpacity = 1.0f;
    ImVec2 m_ViewportPos{ 0, 0 };
    ImVec2 m_ViewportSize{ 1, 1 };
    bool m_SceneViewHovered = false;
    bool m_SceneViewFocused = false;
    bool m_SceneViewVisible = false;
    bool m_CameraCapturingMouse = false;
    bool m_CursorLocked = false;
    ImVec2 m_ViewCubeMin{ 0, 0 }, m_ViewCubeMax{ 0, 0 };
    glm::mat4 m_ViewMatrix{ 1.0f };
    glm::mat4 m_ProjMatrix{ 1.0f };   // Vulkan depth (0..1)
    glm::mat4 m_GizmoProj{ 1.0f };    // OpenGL depth (-1..1) for ImGuizmo
    bool m_LeftDownOnViewport = false;
    ImVec2 m_LeftDownPos{ 0, 0 };
    bool m_Marquee = false;
    EntityId m_PreviewCamera = kNullEntity;

    // Game view state
    bool m_GameViewVisible = false;
    ImVec2 m_GameViewSize{ 1, 1 };
    int m_GameAspect = 0; // 0 free, 1 16:9, 2 16:10, 3 4:3, 4 1:1
    bool m_ShowStats = false;
    bool m_MaximizeOnPlay = false;

    // Hierarchy / inspector state
    EntityId m_RenameEntity = kNullEntity;
    char m_RenameBuffer[128] = {};
    bool m_RenameFocus = false;
    char m_HierarchyFilter[128] = {};
    char m_AddComponentFilter[64] = {};
    EntityId m_LastClicked = kNullEntity;
    std::vector<EntityId> m_HierarchyOrder;     // visible order this frame
    std::vector<EntityId> m_HierarchyOrderPrev; // previous frame, for shift-click range selection
    EntityId m_ScrollToEntity = kNullEntity;

    // Console
    bool m_ConsoleShowInfo = true;
    bool m_ConsoleShowWarn = true;
    bool m_ConsoleShowError = true;
    bool m_ConsoleCollapse = true;
    size_t m_ConsoleLastCount = 0;

    // Project / assets
    std::string m_ProjectFolder = "Assets";
    std::string m_SelectedAsset;          // asset shown in the Inspector (exclusive with entity selection)
    std::string m_RenameAsset;
    char m_AssetRenameBuffer[256] = {};
    bool m_AssetRenameFocus = false;
    std::string m_DeleteAsset;            // pending delete confirmation
    char m_ProjectSearch[128] = {};
    float m_AssetScanTimer = 0.0f;
    std::map<std::string, std::filesystem::file_time_type> m_ScriptStamps;
    std::map<EntityId, std::string> m_ProbeSignatures; // what each reflection probe was last baked with
    void UpdateReflectionProbes(VkCommandBuffer cmd, float time);
    void ResetReflectionProbes();

    // Prefabs
    std::map<std::string, Prefab::Contents> m_PrefabCache;
    std::map<std::string, std::filesystem::file_time_type> m_PrefabStamps;
    std::string m_PrefabModePath; // non-empty while a prefab is open in isolation
    struct PrefabModeState
    {
        Scene scene; // the scene that was open before entering prefab mode
        std::vector<EntityId> selection;
        std::deque<Snapshot> undo;
        std::vector<Snapshot> redo;
        bool sceneDirty = false;
        Prefab::Contents saved;
        bool hasSaved = false;
    };
    PrefabModeState m_PrefabMode;
    std::vector<std::string> m_PendingDrops;
    std::string m_ScriptPreviewPath;
    std::string m_ScriptPreview;

    // Toasts
    struct Toast
    {
        std::string text;
        float age = 0.0f;
        ImGuiID id = 0;
    };
    std::vector<Toast> m_Toasts;
    ImGuiID m_ToastCounter = 1;

    float m_Time = 0.0f;
    float m_FpsAccum = 0.0f;
    int m_FpsFrames = 0;
    float m_Fps = 0.0f;
};
