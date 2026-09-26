// Automated editor test (run with --selftest). Input is injected through ImGui's IO queue, so the real
// editor code paths run (ImGuizmo, camera controls, shortcuts) without touching the OS mouse or keyboard.
#include "editor/Editor.h"
#include "anim/AnimationGraph.h"

#include "core/Log.h"

#include <ImGuizmo.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

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

void Editor::EnableSelfTest(const std::string& captureDir, bool animationOnly)
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
            // Reflections on the metallic sphere (sky + reflection probe).
            if (frame == 157)
            {
                ClearSelection();
                if (Entity* s = find("Sphere"))
                    m_Camera.SetState(glm::vec3(m_Scene.WorldMatrix(s->id)[3]), glm::angleAxis(glm::radians(25.0f), glm::vec3(0, 1, 0)) *
                                      glm::angleAxis(glm::radians(-12.0f), glm::vec3(1, 0, 0)), 2.4f, false);
            }
            if (frame == 185) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/reflections.bmp");
            // Point light shadows at dusk.
            static float sunIntensity = 1.0f;
            if (frame == 186 && light)
            {
                sunIntensity = light->light.intensity;
                light->light.intensity = 0.03f;
                light->transform.SetEuler({ -25.0f, -30.0f, 0.0f });
                m_Camera.SetState(glm::vec3(0.8f, 0.3f, 0.6f), glm::angleAxis(glm::radians(-150.0f), glm::vec3(0, 1, 0)) *
                                  glm::angleAxis(glm::radians(-40.0f), glm::vec3(1, 0, 0)), 7.0f, false);
            }
            if (frame == 215) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/point_shadows.bmp");
            if (frame == 216 && light)
            {
                light->light.intensity = sunIntensity;
                light->transform.SetEuler({ -35.0f, -30.0f, 0.0f });
                m_Camera.SetState(glm::vec3(0, 0.5f, 0), m_Camera.Rotation(), 11.0f, false);
            }
            // Post-processing on / off (Global Volume: bloom, vignette, tonemapping).
            if (frame == 240) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/post_on.bmp");
            if (frame == 241) if (Entity* v = find("Global Volume")) v->volume.enabled = false;
            if (frame == 265) m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/post_off.bmp");
            if (frame == 266)
            {
                if (Entity* v = find("Global Volume")) v->volume.enabled = true;
                // Same view the gizmo tests were written for.
                const glm::quat rot = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(-30.0f), glm::vec3(1, 0, 0));
                if (Entity* s = find("Sphere")) { Select(s->id); m_Camera.SetState(glm::vec3(m_Scene.WorldMatrix(s->id)[3]), rot, 8.0f, false); }
            }
            return frame >= 280;
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

    t.steps.push_back({ "physics (Jolt) + C# collision messages", [this, &t, find](int frame) {
        const std::string path = "Assets/Scripts/PhysicsProbe.cs";
        if (frame == 0)
        {
            std::ofstream(path) << "using TheEngine;\n"
                                   "public class PhysicsProbe : MonoBehaviour\n{\n"
                                   "    public int fixedSteps;\n"
                                   "    public string hitName = \"\";\n"
                                   "    public int triggerCount;\n"
                                   "    public float groundDistance = -1f;\n"
                                   "    void FixedUpdate() { fixedSteps++; }\n"
                                   "    void OnCollisionEnter(Collision c) { if (hitName == \"\") hitName = c.gameObject.name; }\n"
                                   "    void OnTriggerEnter(Collider other) { triggerCount++; }\n"
                                   "    void Update()\n    {\n"
                                   "        if (Physics.Raycast(transform.position + Vector3.down * 0.6f, Vector3.down, out RaycastHit hit, 100f))\n"
                                   "            groundDistance = hit.distance;\n"
                                   "    }\n}\n";
            m_Scripts->RequestCompile();
            t.scan = 0.0f;
            return false;
        }
        if (frame < 5 || m_Scripts->IsCompiling()) return false;
        if (t.scan == 0.0f)
        {
            if (!m_Scripts->FindClass("PhysicsProbe"))
            {
                t.Check(false, "PhysicsProbe.cs compiles");
                return true;
            }
            Entity& trigger = m_Scene.Create("TriggerZone");
            trigger.transform.position = { -3.0f, 2.0f, 3.0f };
            trigger.collider.enabled = true;
            trigger.collider.size = { 2.0f, 0.5f, 2.0f };
            trigger.collider.isTrigger = true;

            Entity& faller = m_Scene.Create("Faller");
            faller.transform.position = { -3.0f, 4.0f, 3.0f };
            faller.meshRenderer.enabled = true;
            faller.meshRenderer.mesh = "Sphere";
            AddDefaultCollider(faller);
            faller.rigidbody.enabled = true;
            ScriptComponent probe;
            probe.className = "PhysicsProbe";
            faller.scripts.push_back(probe);
            EnterPlayMode();
            t.Check(m_Playing && m_Physics.Running() && m_Physics.BodyCount() >= 5, "play mode builds physics bodies from colliders");
            t.scan = 1.0f;
            return false;
        }
        if (t.scan == 1.0f)
        {
            if (m_PlayTime < 3.0f) return false;
            Entity* faller = find("Faller");
            const float y = faller ? faller->transform.position.y : -100.0f;
            char msg[160];
            std::snprintf(msg, sizeof(msg), "Rigidbody falls and rests on the ground's mesh collider (y=%.3f)", y);
            t.Check(y > 0.45f && y < 0.6f, msg);
            auto fields = faller ? m_Scripts->InstanceFields(m_Scripts->InstanceHandle(faller->id, 0)) : std::map<std::string, std::string>{};
            const int fixedSteps = fields.count("fixedSteps") ? std::stoi(fields["fixedSteps"]) : 0;
            std::snprintf(msg, sizeof(msg), "FixedUpdate runs at 50 Hz (%d steps in %.2f s)", fixedSteps, m_PlayTime);
            t.Check(std::abs(fixedSteps - m_PlayTime * 50.0f) < 10.0f, msg);
            t.Check(fields["hitName"] == "Ground", ("OnCollisionEnter(Collision) reports the ground (got '" + fields["hitName"] + "')").c_str());
            t.Check(fields.count("triggerCount") && std::stoi(fields["triggerCount"]) >= 1, "OnTriggerEnter fires when passing through a trigger");
            const float distance = fields.count("groundDistance") ? std::stof(fields["groundDistance"]) : -1.0f;
            std::snprintf(msg, sizeof(msg), "Physics.Raycast hits the ground below (distance %.3f)", distance);
            t.Check(distance > -0.01f && distance < 0.2f, msg);
            ExitPlayMode();
            t.scan = 2.0f;
            return false;
        }
        Entity* faller = find("Faller");
        t.Check(faller && std::fabs(faller->transform.position.y - 4.0f) < 1e-4f && !m_Physics.Running(),
                "stopping play mode restores simulated transforms and stops physics");
        if (faller) m_Scene.Destroy(faller->id);
        if (Entity* trigger = find("TriggerZone")) m_Scene.Destroy(trigger->id);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        m_Scripts->RequestCompile();
        return true;
    } });

    t.steps.push_back({ "prefabs: create, instances, overrides, prefab mode, apply, revert", [this, &t, find](int) {
        const std::string path = "Assets/Prefabs/Capsule.prefab";
        Entity* capsule = find("Capsule");
        if (!capsule) { t.Check(false, "default scene contains 'Capsule'"); return true; }
        const EntityId original = capsule->id;
        const std::string created = CreatePrefab(original, "Assets/Prefabs");
        const Entity* hat = find("Hat");
        t.Check(created == path && std::filesystem::exists(path) && find("Capsule")->prefab == path && hat && hat->prefabId != kNullEntity,
                "dragging an object into the Project window creates a prefab and links the object to it");

        const EntityId a = InstantiatePrefab(path, kNullEntity, nullptr);
        const EntityId b = InstantiatePrefab(path, kNullEntity, nullptr);
        auto child = [this](EntityId root, const char* name) -> Entity* {
            for (Entity& e : m_Scene.entities)
                if (e.parent == root && e.name == name) return &e;
            return nullptr;
        };
        t.Check(child(a, "Hat") && child(b, "Hat") && Prefab::InstanceRoot(m_Scene, child(b, "Hat")->id) == b,
                "instantiating a prefab recreates its hierarchy");

        // Overrides: A recolors its hat, B scales its root.
        const glm::vec3 moved(4.0f, 1.0f, -3.0f);
        m_Scene.Find(a)->transform.position = moved;
        child(a, "Hat")->meshRenderer.color = { 1.0f, 0.0f, 0.0f };
        m_Scene.Find(b)->transform.scale = glm::vec3(2.0f);
        RefreshPrefabOverrides();
        const auto described = Prefab::DescribeOverrides(m_Scene, a);
        t.Check(described.size() == 1 && described[0] == "Hat: Mesh Renderer",
                ("overrides are detected per component (" + (described.empty() ? std::string("none") : described[0]) + ")").c_str());

        // Edit the prefab asset in prefab mode: root metallic, hat color, and a new child.
        OpenPrefabMode(path);
        bool isolated = m_Scene.entities.size() == 2 && !m_PrefabModePath.empty();
        for (Entity& e : m_Scene.entities)
        {
            if (e.parent == kNullEntity) e.meshRenderer.metallic = 0.9f;
            else e.meshRenderer.color = { 0.0f, 0.0f, 1.0f };
        }
        Entity& feather = m_Scene.Create("Feather", m_Scene.entities[0].id);
        feather.transform.position = { 0.0f, 1.5f, 0.0f };
        m_SceneDirty = true;
        ClosePrefabMode();
        t.Check(isolated && m_PrefabModePath.empty() && m_Scene.Find(a) && m_Scene.Find(b), "prefab mode edits the asset in isolation and returns to the scene");

        const Entity* ea = m_Scene.Find(a);
        const Entity* eb = m_Scene.Find(b);
        t.Check(ea->meshRenderer.metallic == 0.9f && eb->meshRenderer.metallic == 0.9f && find("Capsule")->meshRenderer.metallic == 0.9f,
                "prefab changes propagate to every instance");
        t.Check(child(a, "Hat")->meshRenderer.color == glm::vec3(1, 0, 0) && child(b, "Hat")->meshRenderer.color == glm::vec3(0, 0, 1),
                "instance overrides survive prefab changes; other instances take the new value");
        t.Check(glm::length(ea->transform.position - moved) < 1e-4f && eb->transform.scale == glm::vec3(2.0f),
                "root position and overridden scale are kept");
        t.Check(child(a, "Feather") && child(b, "Feather") && child(original, "Feather"), "objects added to the prefab appear in all instances");

        // Apply All from instance A: its hat color and a new smoothness become the prefab's.
        m_Scene.Find(a)->meshRenderer.smoothness = 0.12f;
        ApplyPrefabOverrides(a);
        t.Check(m_Scene.Find(b)->meshRenderer.smoothness == 0.12f && child(b, "Hat")->meshRenderer.color == glm::vec3(1, 0, 0) &&
                    m_Scene.Find(b)->transform.scale == glm::vec3(2.0f),
                "Apply All writes the instance to the prefab and updates the others (keeping their overrides)");

        // Revert All on B drops its scale override.
        RevertPrefabOverrides(b);
        t.Check(m_Scene.Find(b)->transform.scale == find("Capsule")->transform.scale, "Revert All restores prefab values");

        // Stored overrides survive a save/load while the prefab changes on disk.
        m_Scene.Find(b)->transform.scale = glm::vec3(3.0f);
        RefreshPrefabOverrides();
        const std::string scenePath = "Assets/Scenes/_prefabtest.scene";
        m_Scene.Save(scenePath);
        Prefab::Contents contents;
        Prefab::Load(path, contents);
        contents[0].meshRenderer.color = { 0.2f, 0.9f, 0.2f };
        contents[0].transform.scale = glm::vec3(1.5f);
        Prefab::SaveContents(path, contents);
        m_PrefabCache.clear();
        m_PrefabStamps.clear();
        OpenScene(scenePath);
        const Entity* lb = nullptr;
        for (const Entity& e : m_Scene.entities)
            if (e.id == b) lb = &e;
        t.Check(lb && lb->meshRenderer.color == glm::vec3(0.2f, 0.9f, 0.2f) && lb->transform.scale == glm::vec3(3.0f),
                "loading a scene applies prefab changes made on disk but keeps saved overrides");
        std::error_code ec;
        std::filesystem::remove(scenePath, ec);
        return true;
    } });

    t.steps.push_back({ "C# Instantiate(prefab) from a GameObject field", [this, &t, find](int frame) {
        const std::string script = "Assets/Scripts/Spawner.cs";
        if (frame == 0)
        {
            std::ofstream(script) << "using TheEngine;\n"
                                     "public class Spawner : MonoBehaviour\n{\n"
                                     "    public GameObject prefab;\n"
                                     "    public int spawned;\n"
                                     "    void Start()\n    {\n"
                                     "        for (int i = 0; i < 3; ++i)\n"
                                     "            if (Instantiate(prefab, new Vector3(i * 2, 5, -6), Quaternion.identity) != null) spawned++;\n"
                                     "        Instantiate(GameObject.Find(\"Cube\"));\n"
                                     "    }\n}\n";
            m_Scripts->RequestCompile();
            t.scan = 0.0f;
            return false;
        }
        if (frame < 5 || m_Scripts->IsCompiling()) return false;
        if (t.scan == 0.0f)
        {
            const ScriptClassInfo* info = m_Scripts->FindClass("Spawner");
            const bool typed = info && !info->fields.empty() && info->fields[0].type == "GameObject";
            t.Check(typed, "GameObject fields are exposed to the Inspector");
            if (!typed) return true;
            Entity& host = m_Scene.Create("SpawnerHost");
            ScriptComponent sc;
            sc.className = "Spawner";
            sc.fields.push_back({ "prefab", "GameObject", "prefab:Assets/Prefabs/Capsule.prefab" });
            host.scripts.push_back(sc);
            EnterPlayMode();
            t.scan = 1.0f;
            return false;
        }
        if (t.scan == 1.0f)
        {
            if (m_PlayTime < 0.5f) return false;
            int clones = 0, hostClones = 0, bodies = m_Physics.BodyCount();
            for (const Entity& e : m_Scene.entities)
            {
                if (e.name == "Capsule(Clone)" && e.prefab == "Assets/Prefabs/Capsule.prefab") ++clones;
                if (e.name == "Cube(Clone)") ++hostClones;
            }
            char msg[160];
            std::snprintf(msg, sizeof(msg), "Instantiate spawns prefab instances (%d) and clones scene objects (%d)", clones, hostClones);
            t.Check(clones == 3 && hostClones == 1, msg);
            const Entity* first = nullptr;
            for (const Entity& e : m_Scene.entities)
                if (e.name == "Capsule(Clone)") { first = &e; break; }
            t.Check(first && glm::length(first->transform.position - glm::vec3(0, 5, -6)) < 0.5f + 0.5f * 9.81f * 0.25f,
                    "Instantiate places the instance at the given position");
            t.Check(bodies >= 3, "instantiated objects get physics bodies");
            ExitPlayMode();
            t.scan = 2.0f;
            return false;
        }
        if (Entity* host = find("SpawnerHost")) m_Scene.Destroy(host->id);
        std::error_code ec;
        std::filesystem::remove(script, ec);
        m_Scripts->RequestCompile();
        return true;
    } });

    t.steps.push_back({ "rendering: local shadows, reflection probes, post-processing volumes", [this, &t, find](int frame) {
        // Let the views render a few frames (probe baking happens one probe per frame).
        if (frame < 15) return false;
        t.Check(m_Renderer->ShadowedLocalLights() >= 1, "the template's point light renders cube shadow maps");
        const Entity* probe = find("Reflection Probe");
        t.Check(probe && m_Renderer->IsProbeBaked(probe->id), "reflection probes are baked automatically");

        // Local volume blending: full weight inside the box, fading over the blend distance, none beyond it.
        Entity& box = m_Scene.Create("TestVolume");
        box.transform.position = { 50.0f, 0.0f, 0.0f };
        box.volume.enabled = true;
        box.volume.isGlobal = false;
        box.volume.size = glm::vec3(4.0f);
        box.volume.blendDistance = 2.0f;
        box.volume.priority = 10.0f;
        box.volume.settings.colorAdjustments = true;
        box.volume.settings.saturation = -100.0f;
        const float inside = m_Scene.ResolvePostProcess({ 50.0f, 0.0f, 0.0f }).saturation;
        const float half = m_Scene.ResolvePostProcess({ 53.0f, 0.0f, 0.0f }).saturation;
        const float outside = m_Scene.ResolvePostProcess({ 60.0f, 0.0f, 0.0f }).saturation;
        char msg[160];
        std::snprintf(msg, sizeof(msg), "local volumes blend by distance (%.0f / %.0f / %.0f)", inside, half, outside);
        t.Check(inside == -100.0f && std::fabs(half + 50.0f) < 1.0f && outside == 0.0f, msg);
        m_Scene.Destroy(m_Scene.entities.back().id);
        return true;
    } });

    t.steps.push_back({ "post-processing volume changes the image", [this, &t, find](int frame) {
        auto capture = [this](const char* name) {
            const std::string path = (std::filesystem::temp_directory_path() / name).string();
            m_Renderer->CaptureView(SceneRenderer::SceneViewId, path);
            std::ifstream in(path, std::ios::binary);
            return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        static std::vector<unsigned char> with;
        Entity* volume = find("Global Volume");
        if (!volume) { t.Check(false, "template scene has a Global Volume"); return true; }
        if (frame == 0) { volume->volume.settings.vignetteIntensity = 0.6f; return false; }
        if (frame == 5) { with = capture("theengine_post_on.bmp"); volume->volume.enabled = false; return false; }
        if (frame == 10)
        {
            const std::vector<unsigned char> without = capture("theengine_post_off.bmp");
            volume->volume.enabled = true;
            volume->volume.settings.vignetteIntensity = 0.2f;
            double diff = 0.0;
            const size_t n = std::min(with.size(), without.size());
            for (size_t i = 54; i < n; ++i) diff += std::abs(static_cast<int>(with[i]) - static_cast<int>(without[i]));
            diff /= std::max<size_t>(n - 54, 1);
            char msg[128];
            std::snprintf(msg, sizeof(msg), "Global Volume (bloom, vignette) changes the rendered image (mean diff %.2f)", diff);
            t.Check(n > 54 && diff > 1.0, msg);
            return true;
        }
        return false;
    } });

    t.steps.push_back({ "animation: controller state machine, blend trees, root motion", [this, &t](int frame) {
        // The Animator window opens a controller and draws its graph for a few frames first.
        if (frame == 0)
        {
            AnimatorController().Save("Assets/_window.controller");
            OpenAnimatorController("Assets/_window.controller");
            return false;
        }
        if (frame < 5) return false;
        t.Check(EditedController() != nullptr, "Animator window opens Animator Controller assets");
        m_AnimCtrlPath.clear();
        std::error_code removeError;
        std::filesystem::remove("Assets/_window.controller", removeError);

        // Synthetic rig and clips (the FBX importer is exercised by projects with real assets).
        Skeleton skeleton;
        skeleton.names = { "root", "pelvis" };
        skeleton.parents = { -1, 0 };
        skeleton.rest.resize(2);
        skeleton.rest[1].t = glm::vec3(0.0f, 1.0f, 0.0f);
        skeleton.index = { { "root", 0 }, { "pelvis", 1 } };
        skeleton.root = 0;
        skeleton.pelvis = 1;

        auto makeClip = [&](const char* name, glm::vec2 velocity) {
            AnimationClip c;
            c.name = name;
            c.fps = 30.0f;
            c.duration = 1.0f;
            c.frameCount = 31;
            for (int b = 0; b < 2; ++b)
            {
                AnimationClip::Track tr;
                tr.bone = skeleton.names[b];
                tr.rest = skeleton.rest[b];
                tr.frames.assign(31, skeleton.rest[b]);
                c.tracks.push_back(tr);
                c.trackIndex[tr.bone] = b;
            }
            for (int f = 0; f < 31; ++f)
            {
                const glm::vec2 xz = velocity * (f / 30.0f);
                c.tracks[0].frames[f].t = glm::vec3(xz.x, 0.0f, xz.y);
                c.rootXZ.push_back(xz);
                c.rootYaw.push_back(0.0f);
            }
            c.rootTrack = 0;
            c.hasRootMotion = glm::length(velocity) > 0.0f;
            c.averageSpeed = glm::length(velocity);
            return c;
        };
        ClipLibrary& clips = m_Animation.Clips();
        clips.Add("test:idle", makeClip("idle", { 0.0f, 0.0f }));
        clips.Add("test:fwd", makeClip("fwd", { 0.0f, -2.0f }));
        clips.Add("test:right", makeClip("right", { 2.0f, 0.0f }));
        clips.Add("test:bwd", makeClip("bwd", { 0.0f, 2.0f }));
        clips.Add("test:left", makeClip("left", { -2.0f, 0.0f }));

        AnimatorController c;
        c.params = { { "Speed", AnimParamType::Float, 0.0f }, { "X", AnimParamType::Float, 0.0f }, { "Y", AnimParamType::Float, 0.0f },
                     { "Stop", AnimParamType::Trigger, 0.0f } };
        AnimState idle;
        idle.name = "Idle";
        idle.clip = "test:idle";
        AnimState move;
        move.name = "Move";
        move.type = AnimMotionType::BlendTree2D;
        move.paramX = "X";
        move.paramY = "Y";
        move.children = { { "test:fwd", 0, { 0, 1 } }, { "test:right", 0, { 1, 0 } }, { "test:bwd", 0, { 0, -1 } }, { "test:left", 0, { -1, 0 } } };
        c.Base().states = { idle, move };
        c.Base().defaultState = "Idle";
        AnimTransition go;
        go.from = "Idle";
        go.to = "Move";
        go.duration = 0.1f;
        go.conditions = { { "Speed", AnimConditionMode::Greater, 0.1f } };
        AnimTransition stop;
        stop.from = AnimatorController::kAnyState;
        stop.to = "Idle";
        stop.duration = 0.0f;
        stop.conditions = { { "Stop", AnimConditionMode::If, 0.0f } };
        c.Base().transitions = { go, stop };

        // Save / load round trip.
        const std::string path = "Assets/_test.controller";
        AnimatorController loaded;
        const bool io = c.Save(path) && AnimatorController::IsControllerFile(path) && loaded.Load(path) && loaded.Base().states.size() == 2 &&
                        loaded.Base().states[1].children.size() == 4 && loaded.Base().transitions.size() == 2 &&
                        loaded.Base().transitions[1].from == AnimatorController::kAnyState && loaded.params[3].type == AnimParamType::Trigger &&
                        loaded.Base().defaultState == "Idle";
        t.Check(io, "Animator Controller assets save and load (states, blend trees, transitions, parameters)");
        std::error_code ec;
        std::filesystem::remove(path, ec);

        AnimatorInstance a;
        a.Reset(c);
        Pose pose;
        RootMotion motion, total;
        auto run = [&](float seconds) {
            total = {};
            for (float time = 0.0f; time < seconds - 1e-4f; time += 1.0f / 60.0f)
            {
                a.Update(1.0f / 60.0f, clips, skeleton, true, pose, motion);
                total.position += motion.position;
                total.yaw += motion.yaw;
            }
        };
        run(0.5f);
        t.Check(a.CurrentState() == 0 && glm::length(total.position) < 1e-4f, "default state plays; no parameters, no transition");

        a.SetParam("Speed", 1.0f);
        a.SetParam("Y", 1.0f);
        run(0.2f);
        t.Check(a.CurrentState() == 1 && a.NextState() < 0, "Speed > 0.1 transitions Idle -> Move (0.1 s blend)");
        run(1.0f);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "root motion moves forward at the clip's speed (%.3f %.3f m in 1 s)", total.position.x, total.position.z);
        t.Check(std::fabs(total.position.z + 2.0f) < 0.02f && std::fabs(total.position.x) < 1e-3f, msg);
        t.Check(glm::length(pose[0].t) < 0.05f, "root motion is removed from the pose (the object moves instead)");

        a.SetParam("X", 1.0f);
        a.SetParam("Y", 0.0f);
        run(1.0f);
        std::snprintf(msg, sizeof(msg), "2D blend tree picks the matching direction (%.3f %.3f)", total.position.x, total.position.z);
        t.Check(std::fabs(total.position.x - 2.0f) < 0.02f && std::fabs(total.position.z) < 0.02f, msg);

        a.SetParam("X", 0.5f);
        a.SetParam("Y", 0.5f);
        run(1.0f);
        std::snprintf(msg, sizeof(msg), "diagonal input blends neighbours (%.3f %.3f)", total.position.x, total.position.z);
        t.Check(total.position.x > 0.5f && total.position.z < -0.5f && std::fabs(total.position.x + total.position.z) < 0.05f, msg);

        a.SetParam("Stop", 1.0f);
        run(1.0f / 60.0f);
        t.Check(a.CurrentState() == 0 && a.GetParam("Stop") == 0.0f, "Any State trigger transition fires and consumes the trigger");

        return true;
    } });

    t.steps.push_back({ "animation: interruption, damping, layers, formats, validation", [this, &t](int frame) {
        if (frame > 0) return true; // everything below runs synchronously in one frame

        // ---------- Formats: files written by older versions must keep loading ----------
        const std::string v1 =
            "TheEngineAnimator 1\n"
            "param \"Speed\" float 0\n"
            "state \"Idle\" clip \"test:idle\" \"\" \"\" 1 1 40 200\n"
            "state \"Move\" clip \"test:fwd\" \"\" \"\" 1 1 240 200\n"
            "transition \"Idle\" \"Move\" 1 0.75 0.15\n"
            "  condition \"Speed\" greater 0.1\n"
            "default \"Idle\"\n";
        AnimatorController v1c;
        t.Check(v1c.LoadString(v1) && v1c.layers.size() == 1 && v1c.Base().states.size() == 2 &&
                    v1c.Base().transitions.size() == 1 && v1c.Base().transitions[0].hasExitTime &&
                    std::fabs(v1c.Base().transitions[0].duration - 0.15f) < 1e-4f &&
                    v1c.Base().transitions[0].interruption == AnimInterruption::None &&
                    !v1c.Base().transitions[0].ordered && v1c.Base().defaultState == "Idle",
                "version 1 .controller files still load (single layer, no interruption tail)");

        const std::string v2 =
            "TheEngineAnimator 2\n"
            "param \"Speed\" float 0\n"
            "layer \"Base Layer\" 1 override\n"
            "state \"Idle\" clip \"test:idle\" \"\" \"\" 1 1 40 200\n"
            "default \"Idle\"\n"
            "layer \"Upper\" 0.5 additive \"spine_01\"\n"
            "state \"Raise\" clip \"test:raise\" \"\" \"\" 1 1 240 200\n"
            "default \"Raise\"\n";
        AnimatorController v2c;
        t.Check(v2c.LoadString(v2) && v2c.layers.size() == 2 && v2c.layers[1].name == "Upper" &&
                    v2c.layers[1].blending == AnimLayerBlending::Additive &&
                    std::fabs(v2c.layers[1].weight - 0.5f) < 1e-4f && v2c.layers[1].mask.size() == 1 &&
                    v2c.layers[1].mask[0] == "spine_01" && v2c.layers[1].defaultState == "Raise",
                "version 2 .controller files still load (layers, weights, masks, additive)");

        // ---------- Synthetic rig: root / pelvis / spine, with clips that move and deform ----------
        Skeleton sk;
        sk.names = { "root", "pelvis", "spine_01" };
        sk.parents = { -1, 0, 1 };
        sk.rest.resize(3);
        sk.rest[1].t = glm::vec3(0.0f, 1.0f, 0.0f);
        sk.rest[2].t = glm::vec3(0.0f, 0.5f, 0.0f);
        sk.index = { { "root", 0 }, { "pelvis", 1 }, { "spine_01", 2 } };
        sk.root = 0;
        sk.pelvis = 1;

        // Twists the spine over the clip (translation only animates on root/pelvis, see SampleClip).
        auto makeClip = [&](const char* name, glm::vec2 velocity, float pelvisFrom, float pelvisTo, float spineFrom, float spineTo) {
            AnimationClip c;
            c.name = name;
            c.fps = 30.0f;
            c.duration = 1.0f;
            c.frameCount = 31;
            for (int b = 0; b < 3; ++b)
            {
                AnimationClip::Track tr;
                tr.bone = sk.names[b];
                tr.rest = sk.rest[b];
                tr.frames.assign(31, sk.rest[b]);
                if (b == 1)
                {
                    for (int f = 0; f < 31; ++f) tr.frames[f].t.y = pelvisFrom + (pelvisTo - pelvisFrom) * (f / 30.0f);
                }
                else if (b == 2)
                {
                    for (int f = 0; f < 31; ++f)
                    {
                        const float a = spineFrom + (spineTo - spineFrom) * (f / 30.0f);
                        tr.frames[f].r = glm::angleAxis(a, glm::vec3(0.0f, 0.0f, 1.0f));
                    }
                }
                c.tracks.push_back(tr);
                c.trackIndex[tr.bone] = b;
            }
            for (int f = 0; f < 31; ++f)
            {
                const glm::vec2 xz = velocity * (f / 30.0f);
                c.tracks[0].frames[f].t = glm::vec3(xz.x, 0.0f, xz.y);
                c.rootXZ.push_back(xz);
                c.rootYaw.push_back(0.0f);
            }
            c.rootTrack = 0;
            c.hasRootMotion = glm::length(velocity) > 0.0f;
            c.averageSpeed = glm::length(velocity);
            return c;
        };
        ClipLibrary& clips = m_Animation.Clips();
        clips.Add("i:idle", makeClip("idle", { 0.0f, 0.0f }, 1.0f, 1.0f, 0.0f, 0.0f));
        clips.Add("i:fwd", makeClip("fwd", { 0.0f, -2.0f }, 1.0f, 1.0f, 0.0f, 0.0f));
        clips.Add("i:right", makeClip("right", { 2.0f, 0.0f }, 1.0f, 1.0f, 0.0f, 0.0f));
        clips.Add("i:raise", makeClip("raise", { 0.0f, -2.0f }, 1.0f, 1.5f, 0.0f, 0.6f));
        clips.Add("i:lean", makeClip("lean", { 0.0f, -2.0f }, 1.2f, 1.4f, 0.0f, 0.4f));

        Pose pose;
        RootMotion motion, total;
        auto run = [&](AnimatorInstance& instance, float seconds, bool accumulate) {
            if (accumulate) total = {};
            for (float time = 0.0f; time < seconds - 1e-4f; time += 1.0f / 60.0f)
            {
                instance.Update(1.0f / 60.0f, clips, sk, true, pose, motion);
                if (accumulate) total.position += motion.position;
            }
        };
        char msg[200];

        // ---------- Interruption: a running transition can be interrupted by the next state ----------
        AnimatorController ic;
        ic.params = { { "Speed", AnimParamType::Float, 0.0f } };
        AnimState sA; sA.name = "A"; sA.clip = "i:lean";
        AnimState sB; sB.name = "B"; sB.clip = "i:raise";
        ic.Base().states = { sA, sB };
        ic.Base().defaultState = "A";
        AnimTransition ab; ab.from = "A"; ab.to = "B"; ab.duration = 0.4f;
        ab.interruption = AnimInterruption::Next;
        ab.conditions = { { "Speed", AnimConditionMode::Greater, 0.1f } };
        AnimTransition ba; ba.from = "B"; ba.to = "A"; ba.duration = 0.05f;
        ba.conditions = { { "Speed", AnimConditionMode::Less, 0.1f } };
        ic.Base().transitions = { ab, ba };

        AnimatorInstance ai;
        ai.Reset(ic);
        ai.SetParam("Speed", 1.0f);
        run(ai, 0.2f, false); // half way through the 0.4 s blend
        t.Check(ai.CurrentState() == 0 && ai.NextState() == 1 && !ai.TransitionInterrupted(0),
                "a transition blends towards its destination when nothing interrupts it");
        const Pose before = pose;
        ai.SetParam("Speed", 0.0f);
        ai.Update(1.0f / 60.0f, clips, sk, true, pose, motion);
        float jump = 0.0f;
        for (size_t b = 0; b < pose.size(); ++b) jump = std::max(jump, glm::length(pose[b].t - before[b].t));
        std::snprintf(msg, sizeof(msg), "the interrupted blend keeps playing instead of popping (pose jump %.4f m)", jump);
        t.Check(ai.IsInTransition(0) && ai.TransitionInterrupted(0) && jump < 0.05f, msg);
        t.Check(glm::length(motion.position) > 1e-5f, "root motion keeps flowing while a transition is interrupted");
        run(ai, 0.3f, false);
        t.Check(ai.CurrentState() == 0 && ai.NextState() < 0 && !ai.TransitionInterrupted(0),
                "the interrupted chain settles in the state the new transition leads to");

        // interruption = None: the running transition always finishes.
        AnimatorController nc = ic;
        nc.Base().transitions[0].interruption = AnimInterruption::None;
        AnimatorInstance ni;
        ni.Reset(nc);
        ni.SetParam("Speed", 1.0f);
        run(ni, 0.2f, false);
        ni.SetParam("Speed", 0.0f);
        run(ni, 0.1f, false);
        t.Check(ni.NextState() == 1 && !ni.TransitionInterrupted(0), "interruption None: a running transition cannot be cut short");
        ni.SetParam("Speed", 1.0f); // keep the B -> A transition from firing the moment we land in B
        run(ni, 0.3f, false);
        t.Check(ni.CurrentState() == 1 && ni.NextState() < 0, "interruption None: the blend reaches its destination anyway");

        // ---------- Parameter damping ----------
        AnimatorInstance di;
        di.Reset(ic);
        bool dampedOk = true;
        for (int i = 0; i < 18; ++i) dampedOk = dampedOk && di.SetParamDamped("Speed", 1.0f, 0.5f, 1.0f / 60.0f);
        const float half = di.GetParam("Speed");
        for (int i = 0; i < 200; ++i) di.SetParamDamped("Speed", 1.0f, 0.5f, 1.0f / 60.0f);
        const float settled = di.GetParam("Speed");
        std::snprintf(msg, sizeof(msg), "damped parameters approach the target (%.2f after 0.3 s, %.2f after 3.6 s)", half, settled);
        t.Check(dampedOk && half > 0.3f && half < 0.6f && settled > 0.95f, msg);
        di.SetParam("Speed", 0.0f);
        t.Check(di.SetParamDamped("Speed", 1.0f, 0.0f, 1.0f / 60.0f) && di.GetParam("Speed") == 1.0f,
                "damp time 0 snaps to the target");
        t.Check(!di.SetParamDamped("Nope", 1.0f, 0.5f, 1.0f / 60.0f), "damped set reports unknown parameters");

        // ---------- 1D blend tree ----------
        AnimatorController c1d;
        c1d.params = { { "B", AnimParamType::Float, 0.0f } };
        AnimState walk; walk.name = "Walk"; walk.type = AnimMotionType::BlendTree1D;
        walk.paramX = "B";
        walk.children = { { "i:idle", 0.0f, { 0.0f, 0.0f }, 1.0f }, { "i:fwd", 1.0f, { 0.0f, 0.0f }, 1.0f } };
        c1d.Base().states = { walk };
        c1d.Base().defaultState = "Walk";
        AnimatorInstance wi;
        wi.Reset(c1d);
        wi.SetParam("B", 0.5f);
        run(wi, 1.0f, true);
        std::snprintf(msg, sizeof(msg), "1D blend tree interpolates thresholds (%.2f m over 1 s)", total.position.z);
        t.Check(std::fabs(total.position.z + 1.0f) < 0.05f, msg);

        // ---------- Layers: weight, mask, additive reference pose ----------
        AnimatorController lc;
        AnimState base; base.name = "Idle"; base.clip = "i:idle";
        AnimState upper; upper.name = "Raise"; upper.clip = "i:raise";
        upper.speed = 4.0f;
        upper.loop = false;
        lc.Base().states = { base };
        lc.Base().defaultState = "Idle";
        AnimLayer maskLayer;
        maskLayer.name = "Masked";
        maskLayer.mask = { "spine_01" };
        maskLayer.states = { upper };
        maskLayer.defaultState = "Raise";
        lc.layers.push_back(maskLayer);
        AnimatorInstance li;
        li.Reset(lc);
        run(li, 0.3f, false); // upper layer reaches its non-looping end
        const auto spineTwist = [&]() { return 2.0f * std::acos(std::clamp(pose[2].r.w, -1.0f, 1.0f)); };
        std::snprintf(msg, sizeof(msg), "a mask includes the listed bone (spine twist %.2f rad)", spineTwist());
        t.Check(std::fabs(spineTwist() - 0.6f) < 0.01f, msg);
        std::snprintf(msg, sizeof(msg), "a mask leaves the bones outside it alone (pelvis %.2f)", pose[1].t.y);
        t.Check(std::fabs(pose[1].t.y - 1.0f) < 1e-3f, msg);

        lc.layers[1].mask = { "pelvis" };
        li.Reset(lc);
        run(li, 0.3f, false);
        std::snprintf(msg, sizeof(msg), "masking a bone also drives its children (pelvis %.2f, spine twist %.2f rad)", pose[1].t.y,
                      spineTwist());
        t.Check(std::fabs(pose[1].t.y - 1.5f) < 1e-3f && std::fabs(spineTwist() - 0.6f) < 0.01f, msg);

        // Without a mask the whole body takes the layer's pose (the clip ends at pelvis 1.5).
        lc.layers[1].mask.clear();
        li.Reset(lc);
        run(li, 0.3f, false);
        std::snprintf(msg, sizeof(msg), "an unmasked override replaces the base pose (pelvis %.2f)", pose[1].t.y);
        t.Check(std::fabs(pose[1].t.y - 1.5f) < 1e-3f, msg);

        li.SetLayerWeight(1, 0.5f);
        run(li, 0.3f, false);
        std::snprintf(msg, sizeof(msg), "layer weight scales the override (pelvis %.2f)", pose[1].t.y);
        t.Check(std::fabs(pose[1].t.y - 1.25f) < 1e-3f, msg);

        lc.layers[1].blending = AnimLayerBlending::Additive;
        lc.layers[1].states[0].clip = "i:lean"; // starts at 1.2 m and ends at 1.4 m
        li.Reset(lc);
        run(li, 0.3f, false);
        // Reference pose = the layer's state at normalized time 0 (1.2 m): the delta 1.4 - 1.2 is added
        // on top of the base pose (1.0 m).
        std::snprintf(msg, sizeof(msg), "additive layers add the difference to their first frame (pelvis %.2f)", pose[1].t.y);
        t.Check(std::fabs(pose[1].t.y - 1.2f) < 1e-3f, msg);

        // ---------- Exit time on a non-looping state fires once ----------
        AnimatorController xc;
        AnimState long1; long1.name = "Long"; long1.clip = "i:raise"; long1.loop = false;
        AnimState end1; end1.name = "End"; end1.clip = "i:idle";
        xc.Base().states = { long1, end1 };
        xc.Base().defaultState = "Long";
        AnimTransition finish; finish.from = "Long"; finish.to = "End";
        finish.hasExitTime = true;
        finish.exitTime = 0.5f;
        finish.duration = 0.05f;
        xc.Base().transitions = { finish };
        AnimatorInstance xi;
        xi.Reset(xc);
        run(xi, 0.3f, false);
        t.Check(xi.CurrentState() == 0, "exit time 0.5 has not been reached yet in a non-looping state");
        run(xi, 0.5f, false);
        run(xi, 1.0f, false);
        t.Check(xi.CurrentState() == 1 && xi.NextState() < 0, "exit time fires once when the non-looping state passes it");

        // ---------- Root motion stops at the end of a non-looping state ----------
        AnimatorController rc;
        AnimState run1; run1.name = "Run"; run1.clip = "i:fwd"; run1.loop = false;
        rc.Base().states = { run1 };
        rc.Base().defaultState = "Run";
        AnimatorInstance ri;
        ri.Reset(rc);
        run(ri, 2.0f, true);
        std::snprintf(msg, sizeof(msg), "non-looping root motion stops at the clip's end (%.2f m)", total.position.z);
        t.Check(std::fabs(total.position.z + 2.0f) < 0.05f, msg);

        // ---------- Validation ----------
        t.Check(ic.Validate(nullptr, &sk).empty(), "a well formed controller validates without warnings");
        AnimatorController bad;
        bad.params = { { "Speed", AnimParamType::Float, 0.0f } };
        AnimState noClip; noClip.name = "NoClip"; noClip.clip = "missing:clip";
        AnimState emptyTree; emptyTree.name = "EmptyTree"; emptyTree.type = AnimMotionType::BlendTree1D;
        AnimState lonely; lonely.name = "Lonely"; lonely.clip = "i:idle";
        bad.Base().states = { noClip, emptyTree, lonely };
        bad.Base().defaultState = "NoClip";
        AnimTransition broken;
        broken.from = "NoClip";
        broken.to = "EmptyTree";
        broken.conditions = { { "NoSuchParam", AnimConditionMode::Greater, 1.0f } };
        bad.Base().transitions = { broken };
        bad.Base().mask = { "not_a_bone" };
        AnimLayer extra;
        extra.name = "Broken";
        bad.layers.push_back(extra);
        const std::vector<AnimIssue> issues = bad.Validate(nullptr, &sk);
        bool mentionsTree = false, mentionsBone = false, mentionsParam = false, mentionsReachable = false;
        for (const AnimIssue& is : issues)
        {
            mentionsTree |= is.text.find("blend tree") != std::string::npos;
            mentionsBone |= is.text.find("not_a_bone") != std::string::npos;
            mentionsParam |= is.text.find("NoSuchParam") != std::string::npos;
            mentionsReachable |= is.text.find("unreachable") != std::string::npos;
        }
        std::snprintf(msg, sizeof(msg), "validation reports %zu problems (empty tree, mask bone, unknown parameter, unreachable state)",
                      issues.size());
        t.Check(issues.size() >= 4 && mentionsTree && mentionsBone && mentionsParam && mentionsReachable, msg);

        // ---------- Current format round trip, plus version 3 compatibility ----------
        const std::string text = ic.ToString();
        AnimatorController current;
        const bool currentOk = current.LoadString(text) && text.rfind("TheEngineAnimator 4", 0) == 0 &&
                               text.find(" interrupt next ordered 0") != std::string::npos &&
                               current.Base().transitions.size() == 2 &&
                               current.Base().transitions[0].interruption == AnimInterruption::Next &&
                               !current.Base().transitions[0].ordered &&
                               current.Base().transitions[1].interruption == AnimInterruption::None;
        t.Check(currentOk, "version 4 .controller files store and reload the interruption settings");

        const std::string v3 =
            "TheEngineAnimator 3\n"
            "layer \"Base Layer\" 1 override\n"
            "state \"A\" clip \"i:idle\" \"\" \"\" 1 1 40 200\n"
            "state \"B\" clip \"i:raise\" \"\" \"\" 1 1 240 200\n"
            "transition \"A\" \"B\" 0 0.75 0.2 interrupt current ordered 1\n"
            "default \"A\"\n";
        AnimatorController v3c;
        t.Check(v3c.LoadString(v3) && v3c.Base().transitions.size() == 1 &&
                    v3c.Base().transitions[0].interruption == AnimInterruption::Current && v3c.Base().transitions[0].ordered,
                "version 3 .controller files still load (interruption settings, no mask asset)");

        return true;
    } });

    t.steps.push_back({ "animation: Animator window controller undo and redo", [this, &t](int frame) {
        const std::string path = "Assets/_undo.controller";
        if (frame == 0)
        {
            AnimatorController fresh;
            AnimState start;
            start.name = "Start";
            fresh.Base().states = { start };
            fresh.Base().defaultState = "Start";
            fresh.Save(path);
            OpenAnimatorController(path);
            return false;
        }
        AnimatorController* c = EditedController();
        if (!c)
        {
            t.Check(false, "Animator window opens the controller that is edited");
            return true;
        }
        if (frame == 1)
        {
            const std::string original = c->ToString();
            m_AnimPreEdit = original; // what DrawAnimator records before a frame's widgets edit the controller
            AnimState added;
            added.name = "Added";
            c->Base().states.push_back(added);
            MarkAnimEdited();
            const std::string edited = c->ToString();
            const bool undone = AnimUndo() && EditedController()->ToString() == original;
            t.Check(undone && !AnimUndoAvailable() && AnimRedoAvailable(), "controller undo restores the text from before the edit");
            const bool redone = AnimRedo() && EditedController()->ToString() == edited;
            t.Check(redone && AnimUndoAvailable() && !AnimRedoAvailable(), "controller redo re-applies the edit");
            return false;
        }
        if (frame == 2)
        {
            SaveEditedController();
            std::ifstream in(path);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            t.Check(text.find("\"Added\"") != std::string::npos, "the edited controller is written back to disk");
            OpenAnimatorController(std::string());
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return true;
        }
        return true;
    } });

    t.steps.push_back({ "animation: C# Animator API (defaults, active flag)", [this, &t, find](int frame) {
        const std::string script = "Assets/Scripts/AnimApiDefaults.cs";
        if (frame == 0)
        {
            std::ofstream(script) << R"PY(using TheEngine;

// Reads the Animator API before any Animator instance exists (the object has an Animator
// component but no skinned mesh) and round trips the Inspector's runtime-enable flag.
public class AnimApiDefaults : MonoBehaviour
{
    public int layerCount;
    public string layerName = "?";
    public string stateName = "?";
    public string currentStateName = "?";
    public int parameterCount;
    public bool triedParameter;
    public int layerIndex;
    public float layerWeight;
    public float normalizedTime;
    public bool inTransition;
    public bool interrupted;
    public bool animatorEnabled;
    public bool rootMotion;
    public bool activeBefore;
    public bool activeAfterOff;
    public bool activeAfterOn;

    void OnAnimatorPose(AnimationStream stream)
    {
        BonePose pose;
        if (stream.TryGetLocal("head", out pose)) stream.SetLocal("head", pose);
    }

    void Start()
    {
        Animator a = GetComponent<Animator>();
        if (a == null) { Debug.LogError("AnimApiDefaults: this object has no Animator"); return; }
        layerCount = a.layerCount;
        layerName = a.GetLayerName(0);
        stateName = a.StateName(0, false);
        currentStateName = a.currentStateName;
        parameterCount = a.parameterCount;
        Animator.Parameter p;
        triedParameter = a.TryGetParameter(0, out p);
        layerIndex = a.GetLayerIndex("Base Layer");
        layerWeight = a.GetLayerWeight(0);
        Animator.LayerState s = a.GetState(0);
        normalizedTime = s.normalizedTime;
        inTransition = s.inTransition;
        interrupted = s.interrupted;
        animatorEnabled = a.enabled;
        rootMotion = a.applyRootMotion;
        activeBefore = a.active;
        a.active = false;
        activeAfterOff = a.active;
        a.active = true;
        activeAfterOn = a.active;
        a.SetFloat("Speed", 1f, 0.5f, 0.016f); // no instance: a no-op, never a crash
    }
}
)PY";
            m_Scripts->RequestCompile();
            t.scan = 0.0f;
            return false;
        }
        if (frame < 5 || m_Scripts->IsCompiling()) return false;
        if (t.scan == 0.0f)
        {
            if (!m_Scripts->FindClass("AnimApiDefaults"))
            {
                t.Check(false, "AnimApiDefaults.cs compiles");
                return true;
            }
            Entity& host = m_Scene.Create("AnimApiHost");
            host.animator.enabled = true;
            ScriptComponent probe;
            probe.className = "AnimApiDefaults";
            host.scripts.push_back(probe);
            EnterPlayMode();
            t.scan = 1.0f;
            return false;
        }
        if (t.scan == 1.0f)
        {
            if (m_PlayTime < 0.5f) return false;
            Entity* host = find("AnimApiHost");
            auto fields = host ? m_Scripts->InstanceFields(m_Scripts->InstanceHandle(host->id, 0)) : std::map<std::string, std::string>{};
            auto get = [&fields](const char* key) {
                auto it = fields.find(key);
                return it == fields.end() ? std::string("<missing>") : it->second;
            };
            char msg[200];
            std::snprintf(msg, sizeof(msg), "state queries answer with their documented defaults while nothing plays (layers %s, layer '%s', state '%s')",
                          get("layerCount").c_str(), get("layerName").c_str(), get("stateName").c_str());
            t.Check(get("layerCount") == "0" && get("layerName").empty() && get("stateName").empty() && get("currentStateName").empty(), msg);
            std::snprintf(msg, sizeof(msg), "parameter enumeration ends immediately (%s) and TryGetParameter reports %s",
                          get("parameterCount").c_str(), get("triedParameter").c_str());
            t.Check(get("parameterCount") == "0" && get("triedParameter") == "0", msg);
            std::snprintf(msg, sizeof(msg), "layer lookups answer without a controller (index %s, weight %s)",
                          get("layerIndex").c_str(), get("layerWeight").c_str());
            t.Check(get("layerIndex") == "-1" && get("layerWeight") == "0", msg);
            t.Check(get("normalizedTime") == "0" && get("inTransition") == "0" && get("interrupted") == "0",
                    "the layer state struct comes back zeroed (no transition, no interrupted blend)");
            t.Check(get("animatorEnabled") == "1" && get("rootMotion") == "1", "Animator enabled / apply root motion read back from the component");
            std::snprintf(msg, sizeof(msg), "the Inspector's Animator checkbox round-trips (before %s, off %s, on %s)", get("activeBefore").c_str(),
                          get("activeAfterOff").c_str(), get("activeAfterOn").c_str());
            t.Check(get("activeBefore") == "1" && get("activeAfterOff") == "0" && get("activeAfterOn") == "1", msg);
            ExitPlayMode();
            t.scan = 2.0f;
            return false;
        }
        if (Entity* host = find("AnimApiHost")) m_Scene.Destroy(host->id);
        std::error_code ec;
        std::filesystem::remove(script, ec);
        m_Scripts->RequestCompile();
        return true;
    } });

    t.steps.push_back({ "animation: blend mask assets and upper body layering", [this, &t](int frame) {
        if (frame > 0) return true;

        const std::string maskPath = "Assets/_test.mask";
        const std::string yamlPath = "Assets/_unity_test.mask";
        const std::string controllerPath = "Assets/_mask_test.controller";
        const std::string editedPath = "Assets/_edited_test.mask";

        BlendMask source;
        source.Add("spine_01");
        source.Add("hand_r");
        BlendMask parsed;
        const std::string maskText = source.ToString();
        t.Check(parsed.LoadString(maskText) && parsed.bones == source.bones && maskText.rfind("TheEngineMask 1", 0) == 0,
                ".mask text round-trips its ordered include list");
        source.Save(maskPath);
        std::ofstream(yamlPath) << "--- !u!114 &1\nAvatarMask:\n";
        t.Check(BlendMask::IsMaskFile(maskPath) && !BlendMask::IsMaskFile(yamlPath),
                "blend mask detection accepts TheEngine assets and rejects Unity YAML .mask files");

        AnimatorController controller;
        AnimState idle; idle.name = "Idle"; idle.clip = "masktest:base"; idle.loop = false;
        controller.Base().states = { idle };
        controller.Base().defaultState = "Idle";
        AnimLayer upper;
        upper.name = "Upper";
        upper.maskAsset = maskPath;
        upper.RefreshMaskAsset();
        AnimState raise; raise.name = "Raise"; raise.clip = "masktest:upper"; raise.loop = false;
        upper.states = { raise };
        upper.defaultState = "Raise";
        controller.layers.push_back(upper);
        controller.Save(controllerPath);
        AnimatorController loaded;
        std::ifstream controllerFile(controllerPath);
        const std::string controllerText((std::istreambuf_iterator<char>(controllerFile)), std::istreambuf_iterator<char>());
        const bool v4ok = loaded.Load(controllerPath) && controllerText.rfind("TheEngineAnimator 4", 0) == 0 &&
                          controllerText.find("@mask \"Assets/_test.mask\"") != std::string::npos &&
                          loaded.layers.size() == 2 && loaded.layers[1].maskAsset == maskPath &&
                          loaded.layers[1].EffectiveMask() == source.bones;
        t.Check(v4ok, "version 4 controllers preserve a blend mask reference and resolve its bones");

        Skeleton skeleton;
        skeleton.names = { "root", "pelvis", "spine_01" };
        skeleton.parents = { -1, 0, 1 };
        skeleton.rest.resize(3);
        skeleton.rest[1].t.y = 1.0f;
        skeleton.rest[2].t.y = 0.5f;
        skeleton.index = { { "root", 0 }, { "pelvis", 1 }, { "spine_01", 2 } };
        skeleton.root = 0;
        skeleton.pelvis = 1;
        auto maskClip = [&](const char* name, float pelvisY, float spineAngle) {
            AnimationClip clip;
            clip.name = name;
            clip.fps = 1.0f;
            clip.duration = 1.0f;
            clip.frameCount = 2;
            for (int bone = 0; bone < 3; ++bone)
            {
                AnimationClip::Track track;
                track.bone = skeleton.names[bone];
                track.rest = skeleton.rest[bone];
                track.frames = { skeleton.rest[bone], skeleton.rest[bone] };
                clip.trackIndex[track.bone] = bone;
                clip.tracks.push_back(track);
            }
            clip.tracks[1].frames[1].t.y = pelvisY;
            clip.tracks[2].frames[1].r = glm::angleAxis(spineAngle, glm::vec3(0.0f, 0.0f, 1.0f));
            return clip;
        };
        ClipLibrary& clips = m_Animation.Clips();
        clips.Add("masktest:base", maskClip("mask base", 2.0f, 0.0f));
        clips.Add("masktest:upper", maskClip("mask upper", 4.0f, 0.8f));
        AnimatorInstance instance;
        instance.Reset(loaded);
        Pose pose;
        RootMotion motion;
        instance.Update(1.0f, clips, skeleton, false, pose, motion);
        const float spineAngle = 2.0f * std::acos(std::clamp(pose[2].r.w, -1.0f, 1.0f));
        char msg[180];
        std::snprintf(msg, sizeof(msg), "asset mask keeps pelvis from base (%.2f) and spine rotation from upper layer (%.2f rad)",
                      pose[1].t.y, spineAngle);
        t.Check(pose.size() == 3 && std::fabs(pose[1].t.y - 2.0f) < 1e-4f && std::fabs(spineAngle - 0.8f) < 1e-4f, msg);

        AnimatorController missing = controller;
        missing.layers[1].maskAsset = "Assets/_missing_test.mask";
        missing.layers[1].RefreshMaskAsset();
        bool missingIssue = false;
        for (const AnimIssue& issue : missing.Validate(nullptr, &skeleton))
            missingIssue |= issue.text.find("could not be loaded") != std::string::npos;
        t.Check(missingIssue && !missing.layers[1].DrivesWholeBody(),
                "a missing assigned mask reports a warning and drives no bones");

        BlendMask invalid;
        invalid.Add("not_in_rig");
        invalid.Save(maskPath);
        controller.layers[1].RefreshMaskAsset();
        bool boneIssue = false;
        for (const AnimIssue& issue : controller.Validate(nullptr, &skeleton))
            boneIssue |= issue.text.find("not_in_rig") != std::string::npos;
        t.Check(boneIssue, "validation reports blend-mask bones that are not in the rig");

        BlendMask editable;
        editable.Add("spine_01");
        editable.Save(editedPath);
        OpenBlendMask(editedPath);
        m_Mask.Remove("spine_01");
        m_Mask.Add("hand_r");
        SaveBlendMask();
        BlendMask written;
        t.Check(written.Load(editedPath) && written.bones.size() == 1 && written.bones[0] == "hand_r",
                "the Blend Mask editor path writes checkbox-style changes back to the asset");

        m_MaskPath.clear();
        m_Mask = {};
        std::error_code ec;
        for (const std::string& path : { maskPath, yamlPath, controllerPath, editedPath }) std::filesystem::remove(path, ec);
        return true;
    } });

    t.steps.push_back({ "animation: writable stream and playable graph", [this, &t, captureDir](int frame) {
        if (frame > 0) return true;
        Skeleton rig;
        rig.names = { "root", "spine", "ik_hand_r" };
        rig.parents = { -1, 0, 1 };
        rig.rest.resize(3);
        rig.rest[1].t = { 0, 1, 0 };
        rig.rest[2].t = { 1, 0, 0 };
        rig.index = { { "root", 0 }, { "spine", 1 }, { "ik_hand_r", 2 } };
        Pose pose = rig.rest;
        RootMotion motion;
        AnimationStream stream(rig, pose, motion);
        const BoneHandle spine = stream.Bind("spine"), hand = stream.Bind("ik_hand_r");
        const glm::vec3 originalHand(stream.Model(hand)[3]);
        BoneTransform turn = stream.Local(spine);
        turn.r = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
        stream.SetLocal(spine, turn);
        const glm::vec3 turnedHand(stream.Model(hand)[3]);
        const bool moved = glm::length(turnedHand - originalHand) > 0.5f;
        const bool restored = stream.SetModel(hand, glm::translate(glm::mat4(1), originalHand)) &&
                              glm::length(glm::vec3(stream.Model(hand)[3]) - originalHand) < 1e-4f;
        t.Check(moved && restored && !stream.Valid({ &rig, 99 }),
                "stream handles read and write local/model bone transforms after parent edits");

        auto makeClip = [&](const char* name, float handX) {
            AnimationClip clip;
            clip.name = name;
            clip.duration = 1.0f;
            clip.fps = 1.0f;
            clip.frameCount = 2;
            AnimationClip::Track track;
            track.bone = "ik_hand_r";
            track.rest = rig.rest[2];
            track.frames = { rig.rest[2], rig.rest[2] };
            track.frames[1].t.x = handX;
            clip.trackIndex["ik_hand_r"] = 0;
            clip.tracks.push_back(track);
            return clip;
        };
        ClipLibrary clips;
        clips.Add("streamtest:a", makeClip("a", 2.0f));
        clips.Add("streamtest:b", makeClip("b", 4.0f));
        AnimationGraph graph;
        const auto a = graph.AddClip("streamtest:a", 1.0f, false);
        const auto b = graph.AddClip("streamtest:b", 1.0f, false);
        const auto mix = graph.AddMixer(a, b, 0.25f);
        graph.SetOutput(graph.AddJob(mix, [](AnimationStream& output) {
            const BoneHandle bone = output.Bind("ik_hand_r");
            BoneTransform transform = output.Local(bone);
            transform.t.x += 1.0f;
            output.SetLocal(bone, transform);
        }));
        const bool evaluated = graph.Evaluate(1.0f, clips, rig, true, false, pose, motion);
        t.Check(evaluated && pose.size() == 3 && std::abs(pose[2].t.x - 3.5f) < 1e-4f,
                "clip sources, a weighted mixer and a writable job evaluate in graph order");

        const char* aeOverride = std::getenv("THEENGINE_AE_ASSETS");
        const char* profile = std::getenv("USERPROFILE");
        const std::filesystem::path aeAssets = aeOverride ? std::filesystem::path(aeOverride) :
            profile ? std::filesystem::path(profile) / "Documents/Unity Projects/AE Master/Assets/AE" : std::filesystem::path();
        const std::filesystem::path ak = aeAssets / "Weapons/AK/Animations/Character/A_FP_AK_Idle.fbx";
        if (std::filesystem::exists(ak))
        {
            AnimationClip imported;
            const bool ok = LoadFbxClip(ak.string(), "", imported);
            bool hasHands = false, hasIk = false;
            for (const auto& track : imported.tracks)
            {
                hasHands |= track.bone == "hand_l" || track.bone == "hand_r";
                hasIk |= track.bone.find("ik_hand") != std::string::npos;
            }
            t.Check(ok && imported.frameCount > 0 && hasHands && hasIk,
                    "AE folder AK character idle FBX imports hand and IK-target tracks");
        }
        const std::filesystem::path bodySource = aeAssets / "Meshes/Character/Quantum_Body_Full.fbx";
        if (std::filesystem::exists(bodySource))
        {
            const std::filesystem::path body = "Assets/_ae_test/Quantum_Body_Full.fbx";
            std::filesystem::create_directories(body.parent_path());
            std::filesystem::copy_file(bodySource, body, std::filesystem::copy_options::overwrite_existing);
            std::vector<MeshData> meshes;
            ModelAsset model;
            const bool imported = ImportFbxModel(body.string(), meshes, model);
            const Skeleton* skeleton = model.skeleton.get();
            t.Check(imported && skeleton && skeleton->Find("head") >= 0 && skeleton->Find("ik_hand_gun") >= 0 &&
                        skeleton->Find("hand_l") >= 0 && skeleton->Find("hand_r") >= 0,
                    "AE Quantum body supplies AK head, hand and weapon IK bones");
            const ModelAsset* liveModel = m_Res->GetModel(body.string());
            std::string meshRef;
            if (liveModel)
                for (int i = 0; i < liveModel->meshCount; ++i)
                {
                    const std::string candidate = body.string() + "#" + std::to_string(i);
                    const Mesh* mesh = m_Res->GetMesh(candidate);
                    if (mesh && mesh->data.Skinned()) { meshRef = candidate; break; }
                }
            const std::filesystem::path rigPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                                                   "tests/assets/AE_AK.rig";
            if (!meshRef.empty() && std::filesystem::exists(rigPath) && std::filesystem::exists(ak))
            {
                AnimatorController controller;
                AnimState idle; idle.name = "Idle"; idle.clip = ak.string(); idle.loop = true;
                controller.Base().states = { idle };
                controller.Base().defaultState = "Idle";
                const std::string controllerPath = "Assets/_ae_ak_test.controller";
                controller.Save(controllerPath);
                Scene rigScene;
                Entity& character = rigScene.Create("AE AK Character");
                character.meshRenderer.enabled = true;
                character.meshRenderer.mesh = meshRef;
                character.animator.enabled = true;
                character.animator.controller = controllerPath;
                character.animator.rig = rigPath.string();
                AnimationSystem system;
                system.Init(m_Res);
                int streamCalls = 0;
                system.onAnimationStream = [&](EntityId id, AnimationStream& stream) {
                    if (id != character.id) return;
                    ++streamCalls;
                    const BoneHandle weapon = stream.Bind("ik_hand_gun");
                    BoneTransform source = stream.Local(weapon);
                    source.t.x += 0.01f;
                    stream.SetLocal(weapon, source);
                };
                system.Update(rigScene, 0.0f, false);
                const Skeleton* augmented = system.SkeletonOf(character.id);
                glm::mat4 before(1), after(1), sourceWeapon(1), sourceGripL(1), sourceGripR(1);
                glm::mat4 handL(1), gripL(1), handR(1), gripR(1);
                const bool prepared = augmented && augmented->Find("vb_ak_weapon") >= 0 &&
                                      system.BoneModelMatrix(character.id, "vb_ak_weapon", before) &&
                                      system.BoneModelMatrix(character.id, "ik_hand_gun", sourceWeapon) &&
                                      system.BoneModelMatrix(character.id, "ik_hand_l", sourceGripL) &&
                                      system.BoneModelMatrix(character.id, "ik_hand_r", sourceGripR) &&
                                      system.BoneModelMatrix(character.id, "vb_ak_hand_l", gripL) &&
                                      system.BoneModelMatrix(character.id, "vb_ak_hand_r", gripR);
                const glm::quat sourceRotation = glm::normalize(glm::quat_cast(glm::mat3(sourceWeapon)));
                const glm::quat correctedRotation = glm::normalize(glm::quat_cast(glm::mat3(before)));
                glm::quat expectedOffset(1, 0, 0, 0);
                bool hasOffset = false;
                std::ifstream rigFile(rigPath);
                std::string rigLine;
                while (std::getline(rigFile, rigLine))
                {
                    std::istringstream row(rigLine);
                    std::string kind, target, space;
                    float x, y, z, w;
                    if (row >> kind && kind == "prerotate" &&
                        row >> std::quoted(target) >> std::quoted(space) >> x >> y >> z >> w &&
                        target == "vb_ak_weapon" && space == target)
                    {
                        expectedOffset = glm::normalize(glm::quat(w, x, y, z));
                        hasOffset = true;
                        break;
                    }
                }
                const bool offsetBeforeLook = prepared && hasOffset && std::abs(glm::dot(
                    glm::normalize(glm::inverse(sourceRotation) * correctedRotation), expectedOffset)) > 0.999f;
                t.Check(offsetBeforeLook, "AE AK weapon rotation offset follows copy and precedes look");
                t.Check(prepared && glm::length(glm::vec3(sourceGripL[3] - gripL[3])) < 0.0001f &&
                            glm::length(glm::vec3(sourceGripR[3] - gripR[3])) < 0.0001f,
                        "AE AK hand targets copy after offset and before look");
                system.SetLook(character.id, 20.0f, 15.0f);
                system.Update(rigScene, 0.0f, false);
                const bool followed = system.BoneModelMatrix(character.id, "vb_ak_weapon", after) &&
                                      system.BoneModelMatrix(character.id, "hand_l", handL) &&
                                      system.BoneModelMatrix(character.id, "vb_ak_hand_l", gripL) &&
                                      system.BoneModelMatrix(character.id, "hand_r", handR) &&
                                      system.BoneModelMatrix(character.id, "vb_ak_hand_r", gripR);
                const float weaponShift = glm::length(glm::vec3(before[3] - after[3]));
                const float leftError = glm::length(glm::vec3(handL[3] - gripL[3]));
                const float rightError = glm::length(glm::vec3(handR[3] - gripR[3]));
                char status[200];
                std::snprintf(status, sizeof(status), "AE AK rig copies targets before look, bends the head weapon helper (%.3f m), solves arms (L %.3f, R %.3f m)",
                              weaponShift, leftError, rightError);
                t.Check(prepared && followed && streamCalls == 2 && weaponShift > 0.001f &&
                            leftError < 0.1f && rightError < 0.1f, status);
                std::error_code ec;
                std::filesystem::remove(controllerPath, ec);
            }
            std::error_code cleanupError;
            const auto target = std::filesystem::weakly_canonical(body.parent_path(), cleanupError);
            const auto assetsRoot = std::filesystem::weakly_canonical("Assets", cleanupError);
            if (captureDir.empty() && !cleanupError && target.parent_path() == assetsRoot)
                std::filesystem::remove_all(target, cleanupError);
        }
        return true;
    } });

    if (!captureDir.empty())
        t.steps.push_back({ "animation: capture AE AK aim", [this, &t, captureDir](int frame) {
            static EntityId actorId = kNullEntity;
            static glm::vec3 oldPivot;
            static glm::quat oldRotation;
            static float oldDistance = 5.0f;
            const std::filesystem::path body = "Assets/_ae_test/Quantum_Body_Full.fbx";
            const char* aeOverride = std::getenv("THEENGINE_AE_ASSETS");
            const char* profile = std::getenv("USERPROFILE");
            const std::filesystem::path aeAssets = aeOverride ? std::filesystem::path(aeOverride) :
                profile ? std::filesystem::path(profile) / "Documents/Unity Projects/AE Master/Assets/AE" : std::filesystem::path();
            const std::filesystem::path clip = aeAssets / "Weapons/AK/Animations/Character/A_FP_AK_Idle.fbx";
            const std::filesystem::path weaponSource = aeAssets / "Weapons/AK/Animations/Weapon/A_W_AK_Idle.fbx";
            const std::filesystem::path weapon = "Assets/_ae_test/A_W_AK_Idle.fbx";
            const std::filesystem::path rigPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                                                   "tests/assets/AE_AK.rig";
            const std::string controllerPath = "Assets/_ae_ak_visual.controller";
            const std::string metricsPath = captureDir + "/ae_ak_metrics.csv";
            auto recordPose = [&](const char* name, float pitch, float yaw) {
                glm::mat4 weaponBone(1), handL(1), gripL(1), handR(1), gripR(1);
                const bool valid = m_Animation.BoneModelMatrix(actorId, "vb_ak_weapon", weaponBone) &&
                                   m_Animation.BoneModelMatrix(actorId, "hand_l", handL) &&
                                   m_Animation.BoneModelMatrix(actorId, "vb_ak_hand_l", gripL) &&
                                   m_Animation.BoneModelMatrix(actorId, "hand_r", handR) &&
                                   m_Animation.BoneModelMatrix(actorId, "vb_ak_hand_r", gripR);
                if (!valid) return false;
                const glm::vec3 position(weaponBone[3]);
                const glm::vec3 forward = glm::normalize(-glm::vec3(weaponBone[2]));
                if (pitch == 0.0f && yaw == 0.0f)
                    t.Check(glm::dot(forward, glm::vec3(0.0f, 0.0f, -1.0f)) > 0.8f,
                            "AE AK neutral weapon faces character forward (-Z)");
                const float leftError = glm::length(glm::vec3(handL[3] - gripL[3]));
                const float rightError = glm::length(glm::vec3(handR[3] - gripR[3]));
                std::ofstream out(metricsPath, std::ios::app);
                out << name << ',' << pitch << ',' << yaw << ',' << position.x << ',' << position.y << ',' << position.z << ','
                    << forward.x << ',' << forward.y << ',' << forward.z << ',' << leftError << ',' << rightError << '\n';
                return static_cast<bool>(out);
            };
            if (frame == 0)
            {
                if (!std::filesystem::exists(body) || !std::filesystem::exists(clip) || !std::filesystem::exists(weaponSource)) return true;
                std::filesystem::copy_file(weaponSource, weapon, std::filesystem::copy_options::overwrite_existing);
                const ModelAsset* bodyModel = m_Res->GetModel(body.string());
                const ModelAsset* weaponModel = m_Res->GetModel(weapon.string());
                if (!bodyModel || !weaponModel) { t.Check(false, "AE AK visual assets import"); return true; }
                AnimatorController controller;
                AnimState idle; idle.name = "Idle"; idle.clip = clip.string();
                controller.Base().states = { idle };
                controller.Base().defaultState = "Idle";
                controller.Save(controllerPath);
                std::ofstream(metricsPath, std::ios::trunc) <<
                    "pose,pitch_deg,yaw_deg,weapon_x_m,weapon_y_m,weapon_z_m,forward_x,forward_y,forward_z,left_grip_error_m,right_grip_error_m\n";
                oldPivot = m_Camera.Pivot(); oldRotation = m_Camera.Rotation(); oldDistance = m_Camera.Distance();
                Entity& character = m_Scene.Create("AE AK Visual Test");
                actorId = character.id;
                character.transform.position = { 20, 0, 0 };
                character.meshRenderer.enabled = true;
                character.meshRenderer.mesh = body.string() + "#0";
                character.animator.enabled = true;
                character.animator.controller = controllerPath;
                character.animator.rig = rigPath.string();
                for (int i = 1; i < bodyModel->meshCount; ++i)
                {
                    Entity& part = m_Scene.Create("Quantum Part " + std::to_string(i));
                    part.meshRenderer.enabled = true;
                    part.meshRenderer.mesh = body.string() + "#" + std::to_string(i);
                    m_Scene.SetParent(part.id, actorId, false);
                }
                Entity& rifle = m_Scene.Create("AE AK Weapon");
                const EntityId weaponId = rifle.id;
                rifle.meshRenderer.enabled = true;
                rifle.meshRenderer.mesh = weapon.string() + "#0";
                rifle.boneSocket.enabled = true;
                rifle.boneSocket.bone = "vb_ak_weapon";
                m_Scene.SetParent(weaponId, actorId, false);
                for (int i = 1; i < weaponModel->meshCount; ++i)
                {
                    Entity& part = m_Scene.Create("AK Part " + std::to_string(i));
                    part.meshRenderer.enabled = true;
                    part.meshRenderer.mesh = weapon.string() + "#" + std::to_string(i);
                    m_Scene.SetParent(part.id, weaponId, false);
                }
                ClearSelection();
                m_Camera.SetOrthographic(false);
                m_Camera.SetState({ 20.0f, 1.2f, 0.0f }, glm::angleAxis(glm::radians(180.0f), glm::vec3(0, 1, 0)), 3.0f, false);
                return false;
            }
            if (frame == 15)
            {
                m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/ae_ak_neutral.bmp");
                t.Check(recordPose("neutral", 0.0f, 0.0f), "AE AK neutral pose metrics recorded");
            }
            if (frame == 16) m_Animation.SetLook(actorId, 20.0f, 15.0f);
            if (frame == 35)
            {
                m_Renderer->CaptureView(SceneRenderer::SceneViewId, captureDir + "/ae_ak_aim.bmp");
                t.Check(recordPose("aim", 20.0f, 15.0f), "AE AK angled pose metrics recorded");
            }
            if (frame < 36) return false;
            const bool captured = std::filesystem::exists(captureDir + "/ae_ak_neutral.bmp") &&
                                  std::filesystem::exists(captureDir + "/ae_ak_aim.bmp") &&
                                  std::filesystem::exists(metricsPath);
            t.Check(captured, "AE AK neutral and angled aim frames captured for review");
            if (actorId != kNullEntity) m_Scene.Destroy(actorId);
            actorId = kNullEntity;
            m_Camera.SetState(oldPivot, oldRotation, oldDistance, false);
            std::error_code ec;
            std::filesystem::remove(controllerPath, ec);
            const auto target = std::filesystem::weakly_canonical(body.parent_path(), ec);
            const auto assetsRoot = std::filesystem::weakly_canonical("Assets", ec);
            if (!ec && target.parent_path() == assetsRoot) std::filesystem::remove_all(target, ec);
            return true;
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

    if (animationOnly)
        t.steps.erase(std::remove_if(t.steps.begin(), t.steps.end(), [](const SelfTest::Step& step) {
            return std::string(step.name).rfind("animation:", 0) != 0;
        }), t.steps.end());
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
