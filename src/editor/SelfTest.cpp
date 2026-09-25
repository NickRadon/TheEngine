// Automated editor test (run with --selftest). Input is injected through ImGui's IO queue, so the real
// editor code paths run (ImGuizmo, camera controls, shortcuts) without touching the OS mouse or keyboard.
#include "editor/Editor.h"

#include "core/Log.h"

#include <ImGuizmo.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>

struct Editor::SelfTest
{
    struct Step
    {
        const char* name;
        std::function<bool(int frame)> run; // returns true when the step is complete
    };

    std::vector<Step> steps;
    size_t index = 0;
    int stepFrame = 0;
    int totalFrames = 0;
    int passes = 0;
    int failures = 0;
    int errorsAtStart = 0;

    // Scratch state shared between steps.
    glm::vec3 vec{ 0.0f };
    float value = 0.0f;
    glm::quat rot{ 1, 0, 0, 0 };
    ImVec2 origin{ 0, 0 };
    ImVec2 dir{ 1, 0 };
    ImVec2 grab{ 0, 0 };
    float scan = 0.0f;
    bool handleFound = false;

    void Check(bool ok, const char* what)
    {
        (ok ? passes : failures)++;
        if (ok) LOG_INFO("[selftest] PASS  %s", what);
        else LOG_ERROR("[selftest] FAIL  %s", what);
    }
};

namespace
{
    ImGuiIO& IO() { return ImGui::GetIO(); }
    void MouseTo(ImVec2 p) { IO().AddMousePosEvent(p.x, p.y); }
    void MouseButton(int button, bool down) { IO().AddMouseButtonEvent(button, down); }
    void Key(ImGuiKey key, bool down) { IO().AddKeyEvent(key, down); }
    ImVec2 Add(ImVec2 a, ImVec2 b, float s = 1.0f) { return ImVec2(a.x + b.x * s, a.y + b.y * s); }
}

Editor::Editor() = default;
Editor::~Editor() = default;

int Editor::SelfTestFailures() const
{
    return m_Test ? m_Test->failures : 0;
}

void Editor::EnableSelfTest(const std::string& captureDir)
{
    m_Test = std::make_unique<SelfTest>();
    SelfTest& t = *m_Test;
    auto find = [this](const char* name) -> Entity* {
        for (Entity& e : m_Scene.entities)
            if (e.name == name) return &e;
        return nullptr;
    };
    auto viewportCenter = [this]() { return ImVec2(m_ViewportPos.x + m_ViewportSize.x * 0.5f, m_ViewportPos.y + m_ViewportSize.y * 0.55f); };

    t.steps.push_back({ "scripts compiled", [this, &t](int frame) {
        if (frame < 2 || m_Scripts->IsCompiling()) return false;
        if (!m_Scripts->Available())
        {
            t.Check(false, ("C# scripting available (" + m_Scripts->Status() + ")").c_str());
            return true;
        }
        const ScriptClassInfo* rotator = m_Scripts->FindClass("Rotator");
        t.Check(rotator && m_Scripts->FindClass("Bobber"), "template C# scripts compile (Rotator, Bobber)");
        t.Check(rotator && rotator->fields.size() == 1 && rotator->fields[0].name == "degreesPerSecond" &&
                    rotator->fields[0].type == "Vector3" && rotator->fields[0].defaultValue == "0 45 0",
                "public fields and defaults are reflected for the Inspector");
        return true;
    } });

    t.steps.push_back({ "setup", [this, &t, find](int frame) {
        if (frame == 0) t.errorsAtStart = Log::CountOf(LogLevel::Error);
        if (frame < 30) return false; // let docking and the first frames settle
        m_FocusSceneView = true;
        Entity* sphere = find("Sphere");
        t.Check(sphere != nullptr, "default scene contains 'Sphere'");
        if (!sphere) return true;
        Select(sphere->id);
        m_Tool = Tool::Move;
        m_LocalSpace = false;
        m_PivotMode = true;
        m_ShowGizmos = true;
        m_Camera.SetOrthographic(false);
        m_Camera.SetState(glm::vec3(m_Scene.WorldMatrix(sphere->id)[3]), m_Camera.Rotation(), 8.0f, false);
        return true;
    } });

    if (!captureDir.empty())
    {
        // Renders of the scene view for visual inspection (lighting, shadows, anti-aliasing, outline).
        t.steps.push_back({ "capture", [this, captureDir, find](int frame) {
            Entity* light = find("Directional Light");
            if (frame == 20) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/scene_selected.bmp");
            if (frame == 21) { ClearSelection(); m_Camera.SetState(glm::vec3(0, 0.5f, 0), m_Camera.Rotation(), 11.0f, false); }
            if (frame == 40) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/scene_afternoon.bmp");
            if (frame == 41 && light) light->transform.SetEuler({ -12.0f, 60.0f, 0.0f });
            if (frame == 60) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/scene_low_sun.bmp");
            if (frame == 61 && light) light->transform.SetEuler({ -4.0f, 150.0f, 0.0f });
            if (frame == 62)
                m_Camera.SetState(glm::vec3(0, 3.0f, 0), glm::angleAxis(glm::radians(-30.0f), glm::vec3(0, 1, 0)) *
                                  glm::angleAxis(glm::radians(20.0f), glm::vec3(1, 0, 0)), 0.5f, false);
            if (frame == 80) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/sky_sunset_up.bmp");
            if (frame == 81)
                m_Camera.SetState(glm::vec3(0, 3.0f, 0), glm::angleAxis(glm::radians(150.0f), glm::vec3(0, 1, 0)) *
                                  glm::angleAxis(glm::radians(55.0f), glm::vec3(1, 0, 0)), 0.5f, false);
            if (frame == 82 && light) light->transform.SetEuler({ -8.0f, -30.0f, 0.0f });
            if (frame == 100) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/sky_clouds_up.bmp");
            if (frame == 101 && light) light->transform.SetEuler({ -35.0f, -30.0f, 0.0f });
            if (frame == 102) { if (Entity* s = find("Sphere")) { Select(s->id); m_Camera.SetState(glm::vec3(m_Scene.WorldMatrix(s->id)[3]), m_Camera.Rotation(), 8.0f, false); } }
            // Ambient-only light with and without SSAO, to check the occlusion pass.
            if (frame == 115) { if (light) light->light.castShadows = false; m_Camera.SetState(glm::vec3(0, 0.5f, 0), glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(-30.0f), glm::vec3(1, 0, 0)), 7.0f, false); ClearSelection(); }
            if (frame == 135) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/ao_on.bmp");
            if (frame == 136) m_Shading = ShadingMode::AmbientOcclusion;
            if (frame == 146) { m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/ao_buffer.bmp"); m_Shading = ShadingMode::Shaded; m_Scene.sky.ssao = false; }
            if (frame == 155) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/ao_off.bmp");
            if (frame == 156) { if (light) light->light.castShadows = true; m_Scene.sky.ssao = true; }
            if (frame == 157) { if (Entity* s = find("Sphere")) { Select(s->id); m_Camera.SetState(glm::vec3(m_Scene.WorldMatrix(s->id)[3]), m_Camera.Rotation(), 8.0f, false); } }
            return frame >= 170;
        } });
    }

    t.steps.push_back({ "find gizmo X handle", [this, &t, find](int frame) {
        if (frame < 10) return false;
        Entity* sphere = find("Sphere");
        if (frame == 10)
        {
            const glm::vec3 p = glm::vec3(m_Scene.WorldMatrix(sphere->id)[3]);
            ImVec2 tip;
            WorldToScreen(p, t.origin);
            WorldToScreen(p + glm::vec3(1, 0, 0), tip);
            float len = std::hypot(tip.x - t.origin.x, tip.y - t.origin.y);
            t.dir = ImVec2((tip.x - t.origin.x) / len, (tip.y - t.origin.y) / len);
            t.scan = 14.0f;
            MouseTo(Add(t.origin, t.dir, t.scan));
            return false;
        }
        if ((frame - 10) % 3 != 0) return false;
        if (ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X))
        {
            t.grab = IO().MousePos;
            t.handleFound = true;
            t.Check(true, "mouse hovers the gizmo X axis handle");
            return true;
        }
        t.scan += 5.0f;
        if (t.scan > 320.0f)
        {
            t.Check(false, "mouse hovers the gizmo X axis handle");
            return true;
        }
        MouseTo(Add(t.origin, t.dir, t.scan));
        return false;
    } });

    t.steps.push_back({ "drag gizmo", [this, &t, find](int frame) {
        if (!t.handleFound) return true;
        Entity* sphere = find("Sphere");
        if (frame == 0)
        {
            t.vec = glm::vec3(m_Scene.WorldMatrix(sphere->id)[3]);
            MouseButton(ImGuiMouseButton_Left, true);
        }
        else if (frame <= 15)
        {
            MouseTo(Add(t.grab, t.dir, frame * 6.0f));
        }
        else if (frame == 16)
        {
            MouseButton(ImGuiMouseButton_Left, false);
        }
        else if (frame == 22)
        {
            const glm::vec3 now = glm::vec3(m_Scene.WorldMatrix(sphere->id)[3]);
            char msg[160];
            std::snprintf(msg, sizeof(msg), "gizmo drag moves along +X only (dx=%.2f dy=%.3f dz=%.3f)", now.x - t.vec.x,
                          now.y - t.vec.y, now.z - t.vec.z);
            t.Check(now.x - t.vec.x > 0.2f && std::fabs(now.y - t.vec.y) < 1e-3f && std::fabs(now.z - t.vec.z) < 1e-3f, msg);
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "undo / redo", [this, &t, find](int frame) {
        if (!t.handleFound) return true;
        Entity* sphere = find("Sphere");
        if (frame == 0) Undo();
        if (frame == 2)
        {
            sphere = find("Sphere");
            t.Check(std::fabs(m_Scene.WorldMatrix(sphere->id)[3].x - t.vec.x) < 1e-4f, "undo restores the position (one step per drag)");
            Redo();
        }
        if (frame == 4)
        {
            sphere = find("Sphere");
            t.Check(m_Scene.WorldMatrix(sphere->id)[3].x - t.vec.x > 0.2f, "redo re-applies the drag");
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "marquee selection", [this, &t](int frame) {
        const ImVec2 start(m_ViewportPos.x + m_ViewportSize.x * 0.12f, m_ViewportPos.y + m_ViewportSize.y * 0.3f);
        const ImVec2 end(m_ViewportPos.x + m_ViewportSize.x * 0.97f, m_ViewportPos.y + m_ViewportSize.y * 0.97f);
        if (frame == 0) { ClearSelection(); MouseTo(start); }
        else if (frame == 3) MouseButton(ImGuiMouseButton_Left, true);
        else if (frame > 3 && frame <= 14) MouseTo(Add(start, ImVec2(end.x - start.x, end.y - start.y), (frame - 3) / 11.0f));
        else if (frame == 16) MouseButton(ImGuiMouseButton_Left, false);
        else if (frame == 20)
        {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "marquee drag selects several objects (%d selected)", static_cast<int>(m_Selection.size()));
            t.Check(m_Selection.size() >= 3, msg);
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "alt+lmb orbit", [this, &t, viewportCenter](int frame) {
        const ImVec2 c = viewportCenter();
        if (frame == 0) { ClearSelection(); MouseTo(c); }
        else if (frame == 2) { t.rot = m_Camera.Rotation(); t.vec = m_Camera.Pivot(); Key(ImGuiMod_Alt, true); }
        else if (frame == 4) MouseButton(ImGuiMouseButton_Left, true);
        else if (frame > 4 && frame <= 14) MouseTo(ImVec2(c.x + (frame - 4) * 12.0f, c.y));
        else if (frame == 15) MouseButton(ImGuiMouseButton_Left, false);
        else if (frame == 16) Key(ImGuiMod_Alt, false);
        else if (frame == 20)
        {
            const float angle = glm::degrees(glm::angle(glm::inverse(t.rot) * m_Camera.Rotation()));
            const float pivotMove = glm::length(m_Camera.Pivot() - t.vec);
            char msg[128];
            std::snprintf(msg, sizeof(msg), "Alt+LMB orbits around a fixed pivot (%.1f deg, pivot moved %.4f)", angle, pivotMove);
            t.Check(angle > 10.0f && pivotMove < 1e-3f, msg);
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "wheel zoom", [this, &t, viewportCenter](int frame) {
        if (frame == 0) { MouseTo(viewportCenter()); }
        else if (frame == 2) { t.value = m_Camera.Distance(); IO().AddMouseWheelEvent(0.0f, 3.0f); }
        else if (frame == 6)
        {
            t.Check(m_Camera.Distance() < t.value * 0.8f, "mouse wheel zooms towards the pivot");
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "F frames selection", [this, &t, find, viewportCenter](int frame) {
        Entity* cube = find("Cube");
        if (!cube) { t.Check(false, "default scene contains 'Cube'"); return true; }
        if (frame == 0) { Select(cube->id); MouseTo(viewportCenter()); }
        else if (frame == 2) Key(ImGuiKey_F, true);
        else if (frame == 3) Key(ImGuiKey_F, false);
        else if (frame == 45)
        {
            glm::vec3 c;
            float r;
            EntityBounds(cube->id, c, r);
            t.Check(glm::length(m_Camera.Pivot() - c) < 0.05f, "F animates the pivot onto the selection");
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "play mode", [this, &t, find](int frame) {
        Entity* cube = find("Cube");
        if (frame == 0) { t.rot = cube->transform.rotation; EnterPlayMode(); }
        else if (frame == 30)
        {
            t.Check(std::fabs(glm::dot(cube->transform.rotation, t.rot)) < 0.9999f, "Rotator script animates the cube in play mode");
            ExitPlayMode();
        }
        else if (frame == 32)
        {
            t.Check(std::fabs(glm::dot(cube->transform.rotation, t.rot)) > 0.99999f, "stopping play mode restores the edit-mode scene");
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "save / load round trip", [this, &t](int) {
        const std::string path = "Assets/Scenes/_selftest.scene";
        m_Scene.sky.shadowDistance = 123.0f;
        bool ok = m_Scene.Save(path);
        Scene loaded;
        ok = ok && loaded.Load(path);
        ok = ok && loaded.entities.size() == m_Scene.entities.size() && loaded.sky.shadowDistance == 123.0f;
        for (size_t i = 0; ok && i < loaded.entities.size(); ++i)
        {
            const Entity& a = loaded.entities[i];
            const Entity& b = m_Scene.entities[i];
            ok = a.name == b.name && a.parent == b.parent && glm::length(a.transform.position - b.transform.position) < 1e-4f &&
                 a.light.castShadows == b.light.castShadows && a.meshRenderer.mesh == b.meshRenderer.mesh;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        t.Check(ok, "scene save/load round trip preserves entities and settings");
        return true;
    } });

    t.steps.push_back({ "material + texture", [this, &t, find](int) {
        Entity* ground = find("Ground");
        const bool hasMat = ground && ground->meshRenderer.material == "Assets/Materials/Checker.mat";
        t.Check(hasMat, "template ground uses Assets/Materials/Checker.mat");
        if (hasMat)
        {
            const GpuMaterial& m = m_Res->GetMaterial(ground->meshRenderer.material);
            t.Check(m.data.albedoMap == "Assets/Textures/Checker.png" && m_Res->GetTexture(m.data.albedoMap, true) != nullptr,
                    "material loads its albedo texture from disk");
        }
        return true;
    } });

    t.steps.push_back({ "glTF import", [this, &t](int frame) {
        const std::string path = "Assets/Models/Tri.gltf";
        if (frame == 0)
        {
            // A triangle with a red material, buffer embedded as a base64 data URI.
            const float positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(positions);
            static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string encoded;
            for (size_t i = 0; i < sizeof(positions); i += 3)
            {
                const uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
                for (int k = 3; k >= 0; --k) encoded += b64[(n >> (6 * k)) & 63];
            }
            std::ofstream out(path);
            out << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)"
                << R"("nodes":[{"name":"Tri","mesh":0,"translation":[0,1,0]}],)"
                << R"("meshes":[{"name":"TriMesh","primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
                << R"("materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0,"roughnessFactor":0.5}}],)"
                << R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" << encoded << R"("}],)"
                << R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)"
                << R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}]})";
            return false;
        }
        const EntityId root = InstantiateModel(path, kNullEntity, nullptr);
        const Entity* tri = nullptr;
        for (const Entity& e : m_Scene.entities)
            if (e.parent == root && e.name == "Tri") tri = &e;
        t.Check(tri && tri->meshRenderer.enabled && tri->meshRenderer.mesh == path + "#0", "glTF instantiates its node hierarchy with mesh references");
        const Mesh* mesh = tri ? m_Res->GetMesh(tri->meshRenderer.mesh) : nullptr;
        t.Check(mesh && mesh->data.vertices.size() == 3, "glTF mesh data uploaded (3 vertices)");
        MaterialAsset red;
        const bool matOk = tri && red.Load(tri->meshRenderer.material) && red.albedo.r > 0.99f && red.albedo.g < 0.01f;
        t.Check(matOk, "glTF material extracted to a .mat asset");
        return true;
    } });

    t.steps.push_back({ "C# Start/Update + Transform API", [this, &t, find](int frame) {
        Entity* sphere = find("Sphere");
        if (!sphere) return true;
        if (frame == 0)
        {
            // Attach the template's Bobber script (moves along Y using Start() + Mathf.Sin).
            ScriptComponent bob;
            bob.className = "Bobber";
            bob.fields.push_back({ "height", "float", "1" });
            bob.fields.push_back({ "speed", "float", "6" });
            sphere->scripts.push_back(bob);
            t.value = sphere->transform.position.y;
            EnterPlayMode();
        }
        else if (frame == 20)
        {
            const float y = find("Sphere")->transform.position.y;
            char msg[128];
            std::snprintf(msg, sizeof(msg), "Bobber.cs moves the sphere through transform.position (dy=%.2f)", y - t.value);
            t.Check(std::fabs(y - t.value) > 0.05f, msg);
            const int handle = m_Scripts->InstanceHandle(find("Sphere")->id, find("Sphere")->scripts.size() - 1);
            auto fields = m_Scripts->InstanceFields(handle);
            t.Check(fields.count("height") && std::stof(fields["height"]) == 1.0f, "Inspector field values are applied to the C# instance");
            ExitPlayMode();
            find("Sphere")->scripts.pop_back();
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "no errors", [&t](int frame) {
        if (frame < 10) return false;
        const int errors = Log::CountOf(LogLevel::Error) - t.errorsAtStart - t.failures;
        char msg[96];
        std::snprintf(msg, sizeof(msg), "no Vulkan validation / engine errors logged (%d)", errors);
        t.Check(errors == 0, msg);
        return true;
    } });

    t.steps.push_back({ "compile errors block play mode", [this, &t](int frame) {
        const std::string path = "Assets/Scripts/Broken.cs";
        if (frame == 0)
        {
            t.scan = 0.0f;
            std::ofstream(path) << "using TheEngine;\npublic class Broken : MonoBehaviour\n{\n    void Update() { int x = }\n}\n";
            m_Scripts->RequestCompile();
            return false;
        }
        if (frame < 5 || m_Scripts->IsCompiling()) return false;
        if (t.scan == 0.0f)
        {
            bool located = false;
            for (const CompileMessage& m : m_Scripts->Messages())
                if (m.error && m.file == path && m.line == 4) located = true;
            t.Check(m_Scripts->HasCompileErrors() && located, "compile error reported with file and line (Broken.cs:4)");
            EnterPlayMode();
            t.Check(!m_Playing, "play mode is refused while scripts have compile errors");
            std::error_code ec;
            std::filesystem::remove(path, ec);
            m_Scripts->RequestCompile();
            t.scan = 1.0f;
            return false;
        }
        t.Check(!m_Scripts->HasCompileErrors() && m_Scripts->FindClass("Rotator"), "fixing the script recompiles cleanly");
        return true;
    } });
}

void Editor::RunSelfTest()
{
    SelfTest& t = *m_Test;
    if (m_WantsQuit) return;
    t.totalFrames++;
    if (t.totalFrames > 20000)
    {
        t.Check(false, "self test finished before the frame limit");
        m_WantsQuit = true;
        return;
    }
    if (t.index >= t.steps.size())
    {
        LOG_INFO("[selftest] done: %d passed, %d failed", t.passes, t.failures);
        m_WantsQuit = true;
        return;
    }
    if (t.steps[t.index].run(t.stepFrame++))
    {
        t.index++;
        t.stepFrame = 0;
    }
}
