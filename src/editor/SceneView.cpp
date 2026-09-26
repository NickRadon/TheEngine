// Scene view (Unity-style viewport controls, gizmos, picking) and Game view.
#include "editor/Editor.h"

#include "editor/EditorUI.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <im_anim.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>

using EditorUI::Icon;

namespace
{
    constexpr float kViewCubeSize = 96.0f;
    constexpr float kIconPickRadius = 14.0f;

    const char* ShadingName(ShadingMode m)
    {
        switch (m)
        {
        case ShadingMode::Wireframe: return "Wireframe";
        case ShadingMode::ShadedWireframe: return "Shaded Wireframe";
        case ShadingMode::AmbientOcclusion: return "Ambient Occlusion";
        default: return "Shaded";
        }
    }
}

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------
bool Editor::WorldToScreen(const glm::vec3& world, ImVec2& out) const
{
    glm::vec4 clip = m_ProjMatrix * m_ViewMatrix * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-5f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    out = ImVec2(m_ViewportPos.x + (ndc.x * 0.5f + 0.5f) * m_ViewportSize.x,
                 m_ViewportPos.y + (0.5f - ndc.y * 0.5f) * m_ViewportSize.y);
    return ndc.z >= 0.0f && ndc.z <= 1.0f;
}

void Editor::ScreenRay(ImVec2 mouse, glm::vec3& origin, glm::vec3& dir) const
{
    glm::vec2 ndc((mouse.x - m_ViewportPos.x) / m_ViewportSize.x * 2.0f - 1.0f,
                  1.0f - (mouse.y - m_ViewportPos.y) / m_ViewportSize.y * 2.0f);
    glm::mat4 inv = glm::inverse(m_ProjMatrix * m_ViewMatrix);
    glm::vec4 n = inv * glm::vec4(ndc, 0.0f, 1.0f);
    glm::vec4 f = inv * glm::vec4(ndc, 1.0f, 1.0f);
    origin = glm::vec3(n) / n.w;
    dir = glm::normalize(glm::vec3(f) / f.w - origin);
}

EntityId Editor::PickEntity(ImVec2 mouse) const
{
    // Gizmo icons (lights / cameras) take priority, like Unity.
    if (m_ShowGizmos)
    {
        float bestDist = kIconPickRadius;
        EntityId best = kNullEntity;
        for (const Entity& e : m_Scene.entities)
        {
            if (!(e.light.enabled || e.camera.enabled) || !m_Scene.IsActiveInHierarchy(e.id)) continue;
            ImVec2 s;
            if (!WorldToScreen(glm::vec3(m_Scene.WorldMatrix(e.id)[3]), s)) continue;
            float d = std::hypot(s.x - mouse.x, s.y - mouse.y);
            if (d < bestDist) { bestDist = d; best = e.id; }
        }
        if (best) return best;
    }

    glm::vec3 origin, dir;
    ScreenRay(mouse, origin, dir);
    float bestT = 1e30f;
    EntityId best = kNullEntity;
    for (const Entity& e : m_Scene.entities)
    {
        if (!e.meshRenderer.enabled || !m_Scene.IsActiveInHierarchy(e.id)) continue;
        glm::mat4 inv = glm::inverse(m_Scene.WorldMatrix(e.id));
        glm::vec3 lo = glm::vec3(inv * glm::vec4(origin, 1.0f));
        glm::vec3 ld = glm::vec3(inv * glm::vec4(dir, 0.0f));
        const Mesh* mesh = m_Res->GetMesh(e.meshRenderer.mesh);
        if (!mesh) continue;
        float t = RaycastMesh(mesh->data, lo, ld);
        if (t > 0.0f && t < bestT) { bestT = t; best = e.id; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Scene view
// ---------------------------------------------------------------------------
void Editor::SceneViewToolbar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 0));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 4, ImGui::GetCursorPosY() + 3));

    // Tool settings (Unity "Tool Settings" overlay): handle position and rotation.
    ImGui::SetNextItemWidth(78);
    if (ImGui::BeginCombo("##pivot", m_PivotMode ? "Pivot" : "Center"))
    {
        if (ImGui::Selectable("Pivot", m_PivotMode)) m_PivotMode = true;
        if (ImGui::Selectable("Center", !m_PivotMode)) m_PivotMode = false;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Tool handle position (Z)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(78);
    if (ImGui::BeginCombo("##space", m_LocalSpace ? "Local" : "Global"))
    {
        if (ImGui::Selectable("Global", !m_LocalSpace)) m_LocalSpace = false;
        if (ImGui::Selectable("Local", m_LocalSpace)) m_LocalSpace = true;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Tool handle rotation (X)");
    ImGui::SameLine();
    if (ImGui::Button("Snap"))
        ImGui::OpenPopup("SnapSettings");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Increment snapping (hold Ctrl while dragging)");
    if (ImGui::BeginPopup("SnapSettings"))
    {
        ImGui::TextDisabled("Increment Snap (hold Ctrl)");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Move", &m_SnapMove, 0.01f, 0.001f, 100.0f, "%.3g");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Rotate", &m_SnapRotate, 0.5f, 0.1f, 180.0f, "%.3g");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Scale", &m_SnapScale, 0.01f, 0.001f, 10.0f, "%.3g");
        ImGui::EndPopup();
    }

    // Right side: view options.
    const float rightWidth = 140 + 4 * 30 + 36 + 30;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, ImGui::GetWindowWidth() - rightWidth));
    ImGui::SetNextItemWidth(140);
    if (ImGui::BeginCombo("##shading", ShadingName(m_Shading)))
    {
        for (int i = 0; i < 4; ++i)
        {
            auto m = static_cast<ShadingMode>(i);
            bool supported = m == ShadingMode::Shaded || m == ShadingMode::AmbientOcclusion || m_Vk->SupportsWireframe();
            if (ImGui::Selectable(ShadingName(m), m_Shading == m, supported ? 0 : ImGuiSelectableFlags_Disabled))
                m_Shading = m;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    bool mode2D = m_Camera.Is2DMode();
    if (EditorUI::ToggleButton("2D", &mode2D, "Toggle 2D view (orthographic, locked rotation)"))
        m_Camera.Set2DMode(mode2D);
    ImGui::SameLine();
    const ImVec2 btn(26, ImGui::GetFrameHeight());
    if (EditorUI::IconButton("##sky", Icon::Sky, m_ShowSkybox, "Toggle skybox and post-processing", btn)) m_ShowSkybox = !m_ShowSkybox;
    ImGui::SameLine();
    if (EditorUI::IconButton("##grid", Icon::Grid, m_ShowGrid, "Toggle grid", btn)) m_ShowGrid = !m_ShowGrid;
    if (ImGui::BeginPopupContextItem("GridOptions"))
    {
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Opacity", &m_GridOpacity, 0.0f, 1.0f);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (EditorUI::IconButton("##gizmos", Icon::Gizmos, m_ShowGizmos, "Toggle gizmos", btn)) m_ShowGizmos = !m_ShowGizmos;
    ImGui::SameLine();
    if (EditorUI::IconButton("##camsettings", Icon::Camera, false, "Scene camera settings", btn))
        ImGui::OpenPopup("SceneCameraSettings");
    if (ImGui::BeginPopup("SceneCameraSettings"))
    {
        ImGui::TextUnformatted("Scene Camera");
        ImGui::Separator();
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Field of View", &m_Camera.fov, 4.0f, 120.0f, "%.0f");
        ImGui::Checkbox("Dynamic Clipping", &m_Camera.dynamicClipping);
        ImGui::BeginDisabled(m_Camera.dynamicClipping);
        ImGui::SetNextItemWidth(160);
        ImGui::DragFloat("Near", &m_Camera.nearClipSetting, 0.001f, 0.001f, 10.0f, "%.3f");
        ImGui::SetNextItemWidth(160);
        ImGui::DragFloat("Far", &m_Camera.farClipSetting, 10.0f, 1.0f, 1e6f, "%.0f");
        ImGui::EndDisabled();
        ImGui::Checkbox("Camera Easing", &m_Camera.easing);
        ImGui::Checkbox("Camera Acceleration", &m_Camera.acceleration);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Camera Speed", &m_Camera.speed, m_Camera.speedMin, m_Camera.speedMax, "%.2f");
        ImGui::SetNextItemWidth(78);
        ImGui::DragFloat("##min", &m_Camera.speedMin, 0.01f, 0.001f, m_Camera.speedMax, "Min %.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(78);
        ImGui::DragFloat("##max", &m_Camera.speedMax, 0.1f, m_Camera.speedMin, 100.0f, "Max %.1f");
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
}

void Editor::SceneViewToolsOverlay(ImVec2 origin)
{
    struct ToolDef { Tool tool; Icon icon; const char* tip; };
    static const ToolDef tools[] = {
        { Tool::View, Icon::Hand, "View Tool (Q)" },
        { Tool::Move, Icon::Move, "Move Tool (W)" },
        { Tool::Rotate, Icon::Rotate, "Rotate Tool (E)" },
        { Tool::Scale, Icon::Scale, "Scale Tool (R)" },
        { Tool::Rect, Icon::Rect, "Rect Tool (T)" },
        { Tool::Transform, Icon::Transform, "Transform Tool (Y)" },
    };
    const float bs = 28.0f;
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.16f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    const float h = 6 * bs + 5 * 2 + 8 + 10;
    if (ImGui::BeginChild("##ToolsOverlay", ImVec2(bs + 6, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar))
    {
        // Overlay grip (Unity overlays have a small handle on top).
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 8, p.y + 2), ImVec2(p.x + bs - 8, p.y + 2), IM_COL32(110, 110, 110, 255), 2.0f);
        ImGui::Dummy(ImVec2(bs, 6));
        for (const ToolDef& t : tools)
        {
            ImGui::PushID(static_cast<int>(t.tool));
            if (EditorUI::IconButton("##tool", t.icon, m_Tool == t.tool, t.tip, ImVec2(bs, bs)))
                m_Tool = t.tool;
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void Editor::DrawColliderGizmo(ImDrawList* dl, const Entity& e)
{
    // Unity's green collider wireframe for selected objects.
    const ColliderComponent& c = e.collider;
    const ImU32 color = IM_COL32(145, 244, 139, 210);
    const glm::mat4 world = m_Scene.WorldMatrix(e.id);
    glm::vec3 scale, pos, skew;
    glm::vec4 persp;
    glm::quat rot;
    glm::decompose(world, scale, rot, pos, skew, persp);
    scale = glm::abs(scale);
    const glm::mat3 R = glm::mat3_cast(glm::normalize(rot));
    const glm::vec3 center = pos + R * (c.center * scale);

    auto line = [&](const glm::vec3& a, const glm::vec3& b) {
        ImVec2 sa, sb;
        if (WorldToScreen(a, sa) && WorldToScreen(b, sb)) dl->AddLine(sa, sb, color, 1.3f);
    };
    auto arc = [&](const glm::vec3& o, const glm::vec3& a, const glm::vec3& b, float r, float from, float to) {
        const int n = 32;
        for (int i = 0; i < n; ++i)
        {
            const float t0 = from + (to - from) * i / n, t1 = from + (to - from) * (i + 1) / n;
            line(o + (a * std::cos(t0) + b * std::sin(t0)) * r, o + (a * std::cos(t1) + b * std::sin(t1)) * r);
        }
    };
    const glm::vec3 X = R[0], Y = R[1], Z = R[2];
    const float pi = glm::pi<float>();
    switch (c.shape)
    {
    case ColliderShape::Box:
    case ColliderShape::Mesh:
    {
        glm::vec3 half = c.size * scale * 0.5f;
        glm::vec3 boxCenter = center;
        if (c.shape == ColliderShape::Mesh)
        {
            const Mesh* mesh = e.meshRenderer.enabled ? m_Res->GetMesh(e.meshRenderer.mesh) : nullptr;
            if (!mesh) return;
            half = (mesh->data.boundsMax - mesh->data.boundsMin) * scale * 0.5f;
            boxCenter = pos + R * ((mesh->data.boundsMax + mesh->data.boundsMin) * 0.5f * scale);
        }
        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
            corners[i] = boxCenter + X * ((i & 1) ? half.x : -half.x) + Y * ((i & 2) ? half.y : -half.y) + Z * ((i & 4) ? half.z : -half.z);
        const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
        for (auto& edge : edges) line(corners[edge[0]], corners[edge[1]]);
        break;
    }
    case ColliderShape::Sphere:
    {
        const float r = c.radius * std::max({ scale.x, scale.y, scale.z });
        arc(center, X, Y, r, 0, 2 * pi);
        arc(center, Y, Z, r, 0, 2 * pi);
        arc(center, Z, X, r, 0, 2 * pi);
        break;
    }
    case ColliderShape::Capsule:
    {
        const float r = c.radius * std::max(scale.x, scale.z);
        const float half = std::max(c.height * scale.y * 0.5f - r, 0.0f);
        const glm::vec3 top = center + Y * half, bottom = center - Y * half;
        arc(top, X, Z, r, 0, 2 * pi);
        arc(bottom, X, Z, r, 0, 2 * pi);
        arc(top, X, Y, r, 0, pi);
        arc(top, Z, Y, r, 0, pi);
        arc(bottom, X, Y, r, pi, 2 * pi);
        arc(bottom, Z, Y, r, pi, 2 * pi);
        line(top + X * r, bottom + X * r);
        line(top - X * r, bottom - X * r);
        line(top + Z * r, bottom + Z * r);
        line(top - Z * r, bottom - Z * r);
        break;
    }
    }
}

void Editor::SceneViewIcons(ImDrawList* dl)
{
    for (EntityId id : m_Selection)
    {
        const Entity* e = m_Scene.Find(id);
        if (!e || !m_Scene.IsActiveInHierarchy(id)) continue;
        if (e->collider.enabled) DrawColliderGizmo(dl, *e);
        // Reflection probe / local volume boxes (axis aligned, like the renderer uses them).
        auto box = [&](const glm::vec3& size, ImU32 color) {
            const glm::mat4 world = m_Scene.WorldMatrix(id);
            const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
            const glm::vec3 c(world[3]), h = glm::abs(size * scale) * 0.5f;
            glm::vec3 corners[8];
            for (int i = 0; i < 8; ++i)
                corners[i] = c + glm::vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
            const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
            for (auto& edge : edges)
            {
                ImVec2 a, b;
                if (WorldToScreen(corners[edge[0]], a) && WorldToScreen(corners[edge[1]], b)) dl->AddLine(a, b, color, 1.3f);
            }
        };
        if (e->reflectionProbe.enabled) box(e->reflectionProbe.size, IM_COL32(255, 229, 127, 200));
        if (e->volume.enabled && !e->volume.isGlobal) box(e->volume.size, IM_COL32(120, 200, 255, 200));
    }

    for (const Entity& e : m_Scene.entities)
    {
        if (!(e.light.enabled || e.camera.enabled) || !m_Scene.IsActiveInHierarchy(e.id)) continue;
        glm::mat4 world = m_Scene.WorldMatrix(e.id);
        glm::vec3 pos = glm::vec3(world[3]);
        ImVec2 s;
        if (!WorldToScreen(pos, s)) continue;
        const bool selected = IsSelected(e.id);

        auto circle = [&](const glm::vec3& center, const glm::vec3& a, const glm::vec3& b, float radius, ImU32 color) {
            ImVec2 prev;
            bool havePrev = false;
            for (int i = 0; i <= 48; ++i)
            {
                const float ang = i * glm::two_pi<float>() / 48.0f;
                ImVec2 cur;
                const bool ok = WorldToScreen(center + (a * std::cos(ang) + b * std::sin(ang)) * radius, cur);
                if (ok && havePrev) dl->AddLine(prev, cur, color, 1.5f);
                prev = cur;
                havePrev = ok;
            }
        };

        if (e.light.enabled && e.light.type != LightType::Directional)
        {
            const ImU32 gizmo = IM_COL32(255, 235, 110, 200);
            if (selected)
            {
                const glm::vec3 fwd = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
                const glm::vec3 right = glm::normalize(glm::vec3(world * glm::vec4(1, 0, 0, 0)));
                const glm::vec3 up = glm::normalize(glm::vec3(world * glm::vec4(0, 1, 0, 0)));
                const float range = e.light.range;
                if (e.light.type == LightType::Point)
                {
                    // Range sphere (three great circles), like Unity.
                    circle(pos, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), range, gizmo);
                    circle(pos, glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), range, gizmo);
                    circle(pos, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), range, gizmo);
                }
                else
                {
                    // Spot cone: rim at the range plus four edges from the apex.
                    const float radius = range * std::tan(glm::radians(e.light.spotAngle) * 0.5f);
                    const glm::vec3 end = pos + fwd * range;
                    circle(end, right, up, radius, gizmo);
                    for (int i = 0; i < 4; ++i)
                    {
                        const float ang = i * glm::half_pi<float>();
                        ImVec2 a0, a1;
                        if (WorldToScreen(pos, a0) && WorldToScreen(end + (right * std::cos(ang) + up * std::sin(ang)) * radius, a1))
                            dl->AddLine(a0, a1, gizmo, 1.5f);
                    }
                }
            }
            const glm::vec3 c = glm::clamp(e.light.color, 0.0f, 1.0f);
            const ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f + c.r * 0.5f, 0.5f + c.g * 0.5f, 0.5f + c.b * 0.5f, 1.0f));
            dl->AddCircleFilled(s, 7.0f, tint, 16);
            dl->AddCircle(s, 11.0f, selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 230, 200), 16, 1.5f);
            if (e.light.type == LightType::Spot)
            {
                ImVec2 tip;
                const glm::vec3 fwd = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
                if (WorldToScreen(pos + fwd * 0.6f, tip)) dl->AddLine(s, tip, IM_COL32(230, 230, 230, 220), 2.0f);
            }
        }
        else if (e.light.enabled)
        {
            // Unity draws the sun icon and, when selected, yellow rays along the light direction.
            if (selected)
            {
                glm::vec3 fwd = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
                glm::vec3 right = glm::normalize(glm::vec3(world * glm::vec4(1, 0, 0, 0)));
                glm::vec3 up = glm::normalize(glm::vec3(world * glm::vec4(0, 1, 0, 0)));
                const float r = 0.5f;
                for (int i = 0; i < 8; ++i)
                {
                    float a = i * glm::two_pi<float>() / 8.0f;
                    glm::vec3 start = pos + (right * std::cos(a) + up * std::sin(a)) * r;
                    ImVec2 a0, a1;
                    if (WorldToScreen(start, a0) && WorldToScreen(start + fwd * 2.5f, a1))
                        dl->AddLine(a0, a1, IM_COL32(255, 235, 110, 220), 1.5f);
                }
                ImVec2 c0;
                for (int i = 0; i <= 32; ++i)
                {
                    float a = i * glm::two_pi<float>() / 32.0f;
                    ImVec2 c1;
                    if (!WorldToScreen(pos + (right * std::cos(a) + up * std::sin(a)) * r, c1)) continue;
                    if (i > 0) dl->AddLine(c0, c1, IM_COL32(255, 235, 110, 220), 1.5f);
                    c0 = c1;
                }
            }
            EditorUI::DrawIcon(dl, Icon::Sun, s, 26.0f, selected ? IM_COL32(255, 230, 90, 255) : IM_COL32(255, 220, 120, 230));
        }
        if (e.camera.enabled)
        {
            if (selected)
            {
                // Frustum (clamped far distance so it stays readable).
                const CameraComponent& c = e.camera;
                float aspect = m_GameViewSize.x > 1 && m_GameViewSize.y > 1 ? m_GameViewSize.x / m_GameViewSize.y : 16.0f / 9.0f;
                RenderView rv = MakeCameraView(e, aspect, 0.0f);
                glm::mat4 proj = c.orthographic
                    ? glm::orthoRH_ZO(-c.orthoSize * aspect, c.orthoSize * aspect, -c.orthoSize, c.orthoSize, c.nearClip, std::min(c.farClip, 30.0f))
                    : glm::perspectiveRH_ZO(glm::radians(c.fov), aspect, c.nearClip, std::min(c.farClip, 30.0f));
                glm::mat4 inv = glm::inverse(proj * rv.view);
                glm::vec3 corners[8];
                for (int i = 0; i < 8; ++i)
                {
                    glm::vec4 p = inv * glm::vec4((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
                    corners[i] = glm::vec3(p) / p.w;
                }
                const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
                for (auto& edge : edges)
                {
                    ImVec2 a, b;
                    if (WorldToScreen(corners[edge[0]], a) && WorldToScreen(corners[edge[1]], b))
                        dl->AddLine(a, b, IM_COL32(230, 230, 230, 200), 1.2f);
                }
            }
            EditorUI::DrawIcon(dl, Icon::Camera, s, 26.0f, selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(210, 210, 210, 230));
        }
    }
}

void Editor::SceneViewGizmo()
{
    if (m_Tool == Tool::View || m_Selection.empty()) return;
    const EntityId active = ActiveEntity();
    if (!m_Scene.Find(active)) return;

    glm::mat4 gizmo = m_Scene.WorldMatrix(active);
    std::vector<EntityId> roots = SelectionRoots();
    if (!m_PivotMode)
    {
        glm::vec3 center(0.0f);
        for (EntityId id : roots)
        {
            glm::vec3 c;
            float r;
            EntityBounds(id, c, r);
            center += c;
        }
        gizmo[3] = glm::vec4(center / static_cast<float>(roots.size()), 1.0f);
    }

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    switch (m_Tool)
    {
    case Tool::Rotate: op = ImGuizmo::ROTATE; break;
    case Tool::Scale: op = ImGuizmo::SCALE; break;
    case Tool::Rect: op = ImGuizmo::BOUNDS; break;
    case Tool::Transform: op = ImGuizmo::UNIVERSAL; break;
    default: break;
    }
    const ImGuizmo::MODE mode = (m_LocalSpace || m_Tool == Tool::Scale || m_Tool == Tool::Rect) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    const bool snapping = ImGui::GetIO().KeyCtrl;
    float snap[3] = { m_SnapMove, m_SnapMove, m_SnapMove };
    if (m_Tool == Tool::Rotate) snap[0] = snap[1] = snap[2] = m_SnapRotate;
    if (m_Tool == Tool::Scale) snap[0] = snap[1] = snap[2] = m_SnapScale;

    float bounds[6] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
    const Entity* activeEntity = m_Scene.Find(active);
    if (const Mesh* mesh = activeEntity && activeEntity->meshRenderer.enabled ? m_Res->GetMesh(activeEntity->meshRenderer.mesh) : nullptr)
    {
        const MeshData& md = mesh->data;
        bounds[0] = md.boundsMin.x; bounds[1] = md.boundsMin.y; bounds[2] = md.boundsMin.z;
        bounds[3] = md.boundsMax.x; bounds[4] = md.boundsMax.y; bounds[5] = md.boundsMax.z;
        // Flat primitives need some thickness for the bounds gizmo.
        for (int i = 0; i < 3; ++i)
            if (bounds[i + 3] - bounds[i] < 1e-3f) { bounds[i] -= 0.001f; bounds[i + 3] += 0.001f; }
    }
    float boundsSnap[3] = { m_SnapMove, m_SnapMove, m_SnapMove };

    const glm::mat4 before = gizmo;
    ImGuizmo::PushID(static_cast<int>(active));
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(m_ViewMatrix), glm::value_ptr(m_GizmoProj), op, mode,
                                              glm::value_ptr(gizmo), nullptr, snapping ? snap : nullptr,
                                              m_Tool == Tool::Rect ? bounds : nullptr,
                                              (m_Tool == Tool::Rect && snapping) ? boundsSnap : nullptr);
    const bool using_ = ImGuizmo::IsUsing(); // must be queried inside the same ID scope
    ImGuizmo::PopID();
    if (using_ && !m_GizmoWasUsing) MarkEdited();
    m_GizmoWasUsing = using_;

    if (changed && using_)
    {
        MarkEdited();
        const glm::mat4 delta = gizmo * glm::inverse(before);
        for (EntityId id : roots)
            m_Scene.SetWorldMatrix(id, delta * m_Scene.WorldMatrix(id));
    }
}

void Editor::SceneViewPicking(bool imageHovered)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool overViewCube = mouse.x >= m_ViewCubeMin.x && mouse.x <= m_ViewCubeMax.x &&
                              mouse.y >= m_ViewCubeMin.y && mouse.y <= m_ViewCubeMax.y;
    const bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();

    if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && m_Tool != Tool::View &&
        !overGizmo && !overViewCube && m_Camera.CurrentDrag() == EditorCamera::DragMode::None)
    {
        m_LeftDownOnViewport = true;
        m_LeftDownPos = mouse;
        m_Marquee = false;
    }
    if (!m_LeftDownOnViewport) return;

    if (m_Camera.CurrentDrag() != EditorCamera::DragMode::None || ImGuizmo::IsUsingAny())
    {
        m_LeftDownOnViewport = false;
        m_Marquee = false;
        return;
    }

    const float dx = mouse.x - m_LeftDownPos.x, dy = mouse.y - m_LeftDownPos.y;
    if (!m_Marquee && dx * dx + dy * dy > 16.0f) m_Marquee = true;

    ImVec2 rmin(std::min(mouse.x, m_LeftDownPos.x), std::min(mouse.y, m_LeftDownPos.y));
    ImVec2 rmax(std::max(mouse.x, m_LeftDownPos.x), std::max(mouse.y, m_LeftDownPos.y));
    if (m_Marquee)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(rmin, rmax, IM_COL32(120, 160, 220, 40));
        dl->AddRect(rmin, rmax, IM_COL32(150, 190, 240, 200));
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        const bool additive = io.KeyCtrl || io.KeyShift;
        if (m_Marquee)
        {
            if (!additive) m_Selection.clear();
            for (const Entity& e : m_Scene.entities)
            {
                if (!m_Scene.IsActiveInHierarchy(e.id)) continue;
                if (!e.meshRenderer.enabled && !e.light.enabled && !e.camera.enabled) continue;
                glm::vec3 c;
                float r;
                EntityBounds(e.id, c, r);
                ImVec2 s;
                if (!WorldToScreen(e.meshRenderer.enabled ? c : glm::vec3(m_Scene.WorldMatrix(e.id)[3]), s)) continue;
                if (s.x >= rmin.x && s.x <= rmax.x && s.y >= rmin.y && s.y <= rmax.y && !IsSelected(e.id))
                    m_Selection.push_back(e.id);
            }
        }
        else
        {
            EntityId hit = PickEntity(mouse);
            if (hit)
            {
                Select(hit, additive);
                m_ScrollToEntity = hit;
            }
            else if (!additive)
            {
                ClearSelection();
            }
        }
        m_LeftDownOnViewport = false;
        m_Marquee = false;
    }
}

void Editor::SceneViewDragDrop()
{
    if (!ImGui::BeginDragDropTarget()) return;
    std::string asset;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool isPrefab = AcceptAssetDrop(".prefab", asset);
    if (isPrefab || AcceptAssetDrop(".glb", asset) || AcceptAssetDrop(".gltf", asset))
    {
        // Place where the mouse ray hits geometry or the ground plane, like Unity.
        glm::vec3 origin, dir;
        ScreenRay(mouse, origin, dir);
        glm::vec3 point = m_Camera.Pivot();
        float bestT = 1e30f;
        for (const Entity& e : m_Scene.entities)
        {
            if (!e.meshRenderer.enabled || !m_Scene.IsActiveInHierarchy(e.id)) continue;
            const Mesh* mesh = m_Res->GetMesh(e.meshRenderer.mesh);
            if (!mesh) continue;
            glm::mat4 inv = glm::inverse(m_Scene.WorldMatrix(e.id));
            float t = RaycastMesh(mesh->data, glm::vec3(inv * glm::vec4(origin, 1.0f)), glm::vec3(inv * glm::vec4(dir, 0.0f)));
            if (t > 0.0f && t < bestT) bestT = t;
        }
        if (bestT < 1e29f) point = origin + dir * bestT;
        else if (std::fabs(dir.y) > 1e-4f && -origin.y / dir.y > 0.0f) point = origin + dir * (-origin.y / dir.y);
        if (isPrefab) InstantiatePrefab(asset, kNullEntity, &point);
        else InstantiateModel(asset, kNullEntity, &point);
    }
    else if (AcceptAssetDrop(".mat", asset))
    {
        // Dropping a material onto an object assigns it (Unity behaviour).
        if (EntityId hit = PickEntity(mouse)) AssignMaterial(hit, asset);
    }
    else if (AcceptAssetDrop(".scene", asset))
    {
        OpenScene(asset);
    }
    ImGui::EndDragDropTarget();
}

void Editor::SceneViewCameraPreview(ImDrawList* dl)
{
    m_PreviewCamera = kNullEntity;
    if (m_Selection.size() != 1) return;
    const Entity* e = m_Scene.Find(m_Selection[0]);
    if (!e || !e->camera.enabled) return;

    const float w = std::clamp(m_ViewportSize.x * 0.25f, 160.0f, 320.0f);
    const float h = w * 9.0f / 16.0f;
    m_Renderer->EnsureSize(SceneRenderer::PreviewViewId, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    m_PreviewCamera = e->id;

    ImVec2 max(m_ViewportPos.x + m_ViewportSize.x - 10, m_ViewportPos.y + m_ViewportSize.y - 10);
    ImVec2 min(max.x - w, max.y - h);
    const float title = ImGui::GetTextLineHeight() + 6;
    dl->AddRectFilled(ImVec2(min.x - 3, min.y - title), ImVec2(max.x + 3, max.y + 3), IM_COL32(40, 40, 40, 240), 4.0f);
    dl->AddText(ImVec2(min.x + 2, min.y - title + 3), IM_COL32(210, 210, 210, 255), e->name.c_str());
    dl->AddImage(m_Renderer->Texture(SceneRenderer::PreviewViewId), min, max);
}

void Editor::DrawSceneView(float dt)
{
    if (m_FocusSceneView)
    {
        ImGui::SetNextWindowFocus();
        m_FocusSceneView = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    m_SceneViewVisible = open;
    if (!open)
    {
        m_SceneViewHovered = false;
        m_PreviewCamera = kNullEntity;
        ImGui::End();
        return;
    }
    m_SceneViewFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    SceneViewToolbar();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 4.0f);
    size.y = std::max(size.y, 4.0f);
    m_ViewportPos = pos;
    m_ViewportSize = size;
    m_Renderer->EnsureSize(SceneRenderer::SceneViewId, static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));

    // The viewport is a plain image, not a button: ImGuizmo only activates when no ImGui item is hovered/active.
    // (Window moving is restricted to title bars, so clicks here never drag the window.)
    ImGui::Image(m_Renderer->Texture(SceneRenderer::SceneViewId), size);
    const bool hovered = ImGui::IsItemHovered();
    m_SceneViewHovered = hovered;
    SceneViewDragDrop();

    // Camera controls (Unity scene view navigation).
    EditorCamera::Input input;
    input.hovered = hovered;
    input.allowKeyboard = (hovered || m_SceneViewFocused) && !TextInputActive();
    input.handTool = m_Tool == Tool::View;
    input.viewportHeight = size.y;
    input.gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();
    m_CameraCapturingMouse = m_Camera.Update(input, dt);

    // Unity keeps dragging when the cursor leaves the view; lock the cursor for unlimited movement.
    const bool wantLock = m_CameraCapturingMouse;
    if (wantLock != m_CursorLocked)
    {
        glfwSetInputMode(m_Window, GLFW_CURSOR, wantLock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        m_CursorLocked = wantLock;
    }

    const float aspect = size.x / size.y;
    m_ViewMatrix = m_Camera.View();
    m_ProjMatrix = m_Camera.Projection(aspect, true);
    m_GizmoProj = m_Camera.Projection(aspect, false);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Gizmos
    ImGuizmo::SetDrawlist(dl);
    ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
    ImGuizmo::SetOrthographic(m_Camera.OrthoBlend() > 0.5f);
    ImGuizmo::Enable(!m_CameraCapturingMouse);
    if (m_ShowGizmos) SceneViewIcons(dl);
    SceneViewGizmo();

    // Scene orientation gizmo (top-right) - click axes to look along them.
    m_ViewCubeMin = ImVec2(pos.x + size.x - kViewCubeSize - 6, pos.y + 6);
    m_ViewCubeMax = ImVec2(m_ViewCubeMin.x + kViewCubeSize, m_ViewCubeMin.y + kViewCubeSize);
    if (!m_Camera.Is2DMode())
    {
        glm::mat4 view = m_ViewMatrix;
        ImGuizmo::ViewManipulate(glm::value_ptr(view), m_Camera.Distance(), m_ViewCubeMin, ImVec2(kViewCubeSize, kViewCubeSize), 0x00000000);
        if (view != m_ViewMatrix && !m_CameraCapturingMouse)
        {
            m_Camera.SetView(view);
            m_ViewMatrix = m_Camera.View();
        }

        // Projection label under the gizmo: click toggles Persp/Iso (like clicking Unity's center cube).
        const char* axis = m_Camera.AxisViewName();
        char label[48];
        std::snprintf(label, sizeof(label), "%s%s%s", axis ? axis : "", axis ? " " : "", m_Camera.Orthographic() ? "Iso" : "Persp");
        ImVec2 ts = ImGui::CalcTextSize(label);
        ImVec2 lp(m_ViewCubeMin.x + (kViewCubeSize - ts.x) * 0.5f, m_ViewCubeMax.y - 2);
        ImGui::SetCursorScreenPos(ImVec2(lp.x - 6, lp.y - 2));
        if (ImGui::InvisibleButton("##projToggle", ImVec2(ts.x + 12, ts.y + 4)))
            m_Camera.SetOrthographic(!m_Camera.Orthographic());
        const bool lh = ImGui::IsItemHovered();
        EditorUI::DrawIcon(dl, m_Camera.Orthographic() ? EditorUI::Icon::Menu : EditorUI::Icon::Play,
                           ImVec2(lp.x - 10, lp.y + ts.y * 0.5f), 9.0f, IM_COL32(200, 200, 200, 200));
        dl->AddText(lp, lh ? IM_COL32(255, 255, 255, 255) : IM_COL32(210, 210, 210, 230), label);
        m_ViewCubeMax.y += ts.y + 6;
    }

    SceneViewPicking(hovered);

    // Cursor feedback for navigation.
    switch (m_Camera.CurrentDrag())
    {
    case EditorCamera::DragMode::Pan: ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); break;
    case EditorCamera::DragMode::None:
        if (hovered && m_Tool == Tool::View) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        break;
    default: break;
    }

    // Flythrough speed overlay (fades with ImAnim).
    {
        float alpha = iam_tween_float(ImHashStr("SceneView"), ImHashStr("speedOverlay"),
                                      m_Camera.speedOverlayTimer > 0.0f ? 1.0f : 0.0f, 0.25f,
                                      iam_ease_preset(iam_ease_out_quad), iam_policy_crossfade, dt, 0.0f);
        if (alpha > 0.01f)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "Camera speed  %.2f", m_Camera.speed);
            ImVec2 ts = ImGui::CalcTextSize(text);
            ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y - 40);
            dl->AddRectFilled(ImVec2(c.x - ts.x * 0.5f - 12, c.y - 6), ImVec2(c.x + ts.x * 0.5f + 12, c.y + ts.y + 6),
                              IM_COL32(30, 30, 30, static_cast<int>(220 * alpha)), 4.0f);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y), IM_COL32(230, 230, 230, static_cast<int>(255 * alpha)), text);
        }
    }

    if (m_Playing)
        dl->AddText(ImVec2(pos.x + 50, pos.y + 8), IM_COL32(255, 255, 255, 120), m_Paused ? "Paused" : "Playing");

    SceneViewCameraPreview(dl);
    SceneViewToolsOverlay(ImVec2(pos.x + 8, pos.y + 8));

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Game view
// ---------------------------------------------------------------------------
void Editor::DrawGameContent()
{
    static const char* aspects[] = { "Free Aspect", "16:9", "16:10", "4:3", "1:1" };
    static const float ratios[] = { 0.0f, 16.0f / 9.0f, 16.0f / 10.0f, 4.0f / 3.0f, 1.0f };

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 4, ImGui::GetCursorPosY() + 3));
    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##display", "Display 1")) { ImGui::Selectable("Display 1", true); ImGui::EndCombo(); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##aspect", &m_GameAspect, aspects, IM_ARRAYSIZE(aspects));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, ImGui::GetWindowWidth() - 230));
    EditorUI::ToggleButton("Maximize On Play", &m_MaximizeOnPlay);
    ImGui::SameLine();
    EditorUI::ToggleButton("Stats", &m_ShowStats);
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 4.0f);
    avail.y = std::max(avail.y, 4.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + avail.y), IM_COL32(20, 20, 20, 255));
    ImGui::InvisibleButton("##gameview", avail);

    ImVec2 size = avail;
    if (ratios[m_GameAspect] > 0.0f)
    {
        if (avail.x / avail.y > ratios[m_GameAspect]) size.x = avail.y * ratios[m_GameAspect];
        else size.y = avail.x / ratios[m_GameAspect];
    }
    ImVec2 min(pos.x + (avail.x - size.x) * 0.5f, pos.y + (avail.y - size.y) * 0.5f);
    m_GameImageMin = min;
    m_GameImageSize = size;
    m_GameViewFocused = m_Playing && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    m_GameViewSize = size;
    m_Renderer->EnsureSize(SceneRenderer::GameViewId, static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));

    if (MainCamera())
    {
        dl->AddImage(m_Renderer->Texture(SceneRenderer::GameViewId), min, ImVec2(min.x + size.x, min.y + size.y));
    }
    else
    {
        const char* msg = "Display 1\nNo cameras rendering";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(pos.x + (avail.x - ts.x) * 0.5f, pos.y + (avail.y - ts.y) * 0.5f), IM_COL32(200, 200, 200, 255), msg);
    }

    if (m_ShowStats)
    {
        char stats[256];
        std::snprintf(stats, sizeof(stats), "Statistics\nFPS: %.1f (%.2f ms)\nDraw calls: %u\nTris: %u\nResolution: %dx%d",
                      m_Fps, m_Fps > 0 ? 1000.0f / m_Fps : 0.0f, m_Renderer->DrawCalls(), m_Renderer->Triangles(),
                      static_cast<int>(size.x), static_cast<int>(size.y));
        ImVec2 ts = ImGui::CalcTextSize(stats);
        ImVec2 smin(min.x + size.x - ts.x - 22, min.y + 10);
        dl->AddRectFilled(smin, ImVec2(smin.x + ts.x + 12, smin.y + ts.y + 10), IM_COL32(20, 20, 20, 200), 4.0f);
        dl->AddText(ImVec2(smin.x + 6, smin.y + 5), IM_COL32(230, 230, 230, 255), stats);
    }
}

void Editor::DrawGameView()
{
    const bool maximized = m_Playing && m_MaximizeOnPlay;
    if (m_FocusGameView && !maximized)
    {
        ImGui::SetNextWindowFocus();
    }
    m_FocusGameView = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    m_GameViewVisible = open || maximized;
    if (open && !maximized) DrawGameContent();
    else if (open) ImGui::TextDisabled("Maximized");
    ImGui::End();

    if (maximized)
    {
        // Covers the dock area while playing, like Unity's "Maximize On Play".
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Game##Maximized", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoTitleBar);
        ImGui::PopStyleVar();
        DrawGameContent();
        ImGui::End();
    }
}
