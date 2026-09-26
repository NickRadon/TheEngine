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
        c.states = { idle, move };
        c.defaultState = "Idle";
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
        c.transitions = { go, stop };

        // Save / load round trip.
        const std::string path = "Assets/_test.controller";
        AnimatorController loaded;
        const bool io = c.Save(path) && AnimatorController::IsControllerFile(path) && loaded.Load(path) && loaded.states.size() == 2 &&
                        loaded.states[1].children.size() == 4 && loaded.transitions.size() == 2 && loaded.transitions[1].from == AnimatorController::kAnyState &&
                        loaded.params[3].type == AnimParamType::Trigger && loaded.defaultState == "Idle";
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
