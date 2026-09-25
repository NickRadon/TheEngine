#include "editor/EditorCamera.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <im_anim.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kLookSensitivity = 0.22f;  // degrees per pixel
    constexpr float kOrbitSensitivity = 0.35f; // degrees per pixel

    glm::quat MakeRotation(float yawDeg, float pitchDeg)
    {
        return glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0)) *
               glm::angleAxis(glm::radians(pitchDeg), glm::vec3(1, 0, 0));
    }

    // Rotate by yaw (world up) and pitch (camera right) without flipping over the poles.
    glm::quat YawPitch(const glm::quat& rot, float yawDeg, float pitchDeg)
    {
        glm::quat yawed = glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0)) * rot;
        glm::vec3 right = yawed * glm::vec3(1, 0, 0);
        glm::quat pitched = glm::normalize(glm::angleAxis(glm::radians(pitchDeg), right) * yawed);
        if ((pitched * glm::vec3(0, 1, 0)).y < 0.0f) return glm::normalize(yawed); // would go past vertical
        return pitched;
    }
}

EditorCamera::EditorCamera()
{
    m_Rotation = MakeRotation(35.0f, -25.0f);
}

glm::vec3 EditorCamera::Position() const
{
    return m_Pivot - Forward() * m_Distance;
}

glm::mat4 EditorCamera::View() const
{
    return glm::lookAt(Position(), m_Pivot, Up());
}

float EditorCamera::NearClip() const
{
    return dynamicClipping ? std::clamp(m_Distance * 0.002f, 0.001f, 1.0f) : nearClipSetting;
}

float EditorCamera::FarClip() const
{
    return dynamicClipping ? std::max(m_Distance * 1000.0f, 2000.0f) : farClipSetting;
}

glm::mat4 EditorCamera::Projection(float aspect, bool zeroToOneDepth) const
{
    const float n = NearClip(), f = FarClip();
    const float fovRad = glm::radians(fov);
    glm::mat4 persp = zeroToOneDepth ? glm::perspectiveRH_ZO(fovRad, aspect, n, f)
                                     : glm::perspectiveRH_NO(fovRad, aspect, n, f);
    if (m_OrthoBlend <= 0.0f) return persp;

    const float h = m_Distance * std::tan(fovRad * 0.5f);
    const float depth = std::max(f, m_Distance * 2.0f);
    glm::mat4 ortho = zeroToOneDepth ? glm::orthoRH_ZO(-h * aspect, h * aspect, -h, h, -depth, depth)
                                     : glm::orthoRH_NO(-h * aspect, h * aspect, -h, h, -depth, depth);
    if (m_OrthoBlend >= 1.0f) return ortho;
    // Unity-style persp <-> iso transition: blend the matrices.
    const float t = m_OrthoBlend * m_OrthoBlend;
    return persp * (1.0f - t) + ortho * t;
}

void EditorCamera::ClipRange(float& nearClip, float& farClip) const
{
    nearClip = NearClip();
    farClip = FarClip();
    if (m_OrthoBlend >= 0.5f)
    {
        const float depth = std::max(farClip, m_Distance * 2.0f);
        nearClip = -depth;
        farClip = depth;
    }
}

void EditorCamera::SetOrthographic(bool ortho)
{
    m_Ortho = ortho;
}

void EditorCamera::Set2DMode(bool enabled)
{
    if (m_2DMode == enabled) return;
    m_2DMode = enabled;
    if (enabled)
    {
        m_Saved3DRot = m_Rotation;
        m_Saved3DOrtho = m_Ortho;
        m_Ortho = true;
        SetState(m_Pivot, glm::quat(1, 0, 0, 0), m_Distance, true);
    }
    else
    {
        m_Ortho = m_Saved3DOrtho;
        SetState(m_Pivot, m_Saved3DRot, m_Distance, true);
    }
}

void EditorCamera::StartAnimation(const glm::vec3& pivot, const glm::quat& rotation, float distance, float duration)
{
    m_FromPivot = m_Pivot;
    m_FromRot = m_Rotation;
    m_FromDist = m_Distance;
    m_ToPivot = pivot;
    m_ToRot = glm::dot(m_FromRot, rotation) < 0.0f ? -rotation : rotation;
    m_ToDist = distance;
    m_AnimDuration = duration;
    m_AnimT = 0.0f;
}

void EditorCamera::SetState(const glm::vec3& pivot, const glm::quat& rotation, float distance, bool animate)
{
    if (animate)
    {
        StartAnimation(pivot, rotation, distance, 0.35f);
        return;
    }
    m_AnimT = 1.0f;
    m_Pivot = pivot;
    m_Rotation = glm::normalize(rotation);
    m_Distance = distance;
}

void EditorCamera::Frame(const glm::vec3& center, float radius, bool animate)
{
    radius = std::max(radius, 0.1f);
    float distance = radius / std::sin(glm::radians(fov) * 0.5f);
    SetState(center, m_Rotation, distance, animate);
}

void EditorCamera::LookAlong(const glm::vec3& forward, const glm::vec3& up, bool animate)
{
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f), forward, up);
    glm::quat rot = glm::quat_cast(glm::inverse(view));
    SetState(m_Pivot, rot, m_Distance, animate);
}

void EditorCamera::AlignWith(const glm::vec3& position, const glm::quat& rotation)
{
    glm::vec3 forward = rotation * glm::vec3(0, 0, -1);
    SetState(position + forward * m_Distance, rotation, m_Distance, true);
}

void EditorCamera::SetView(const glm::mat4& view)
{
    glm::mat4 world = glm::inverse(view);
    glm::vec3 pos = glm::vec3(world[3]);
    m_Rotation = glm::normalize(glm::quat_cast(glm::mat3(world)));
    m_Pivot = pos + Forward() * m_Distance;
    m_AnimT = 1.0f;
}

const char* EditorCamera::AxisViewName() const
{
    const glm::vec3 f = Forward();
    const float e = 0.999f;
    if (f.y < -e) return "Top";
    if (f.y > e) return "Bottom";
    if (f.z < -e) return "Front";
    if (f.z > e) return "Back";
    if (f.x < -e) return "Right";
    if (f.x > e) return "Left";
    return nullptr;
}

int EditorCamera::GridPlane() const
{
    if (!m_Ortho) return 0;
    const glm::vec3 f = glm::abs(Forward());
    if (f.z > 0.999f) return 1;
    if (f.x > 0.999f) return 2;
    return 0;
}

bool EditorCamera::Update(const Input& in, float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    dt = std::min(dt, 0.1f);

    // Animated persp/iso switch, driven by ImAnim.
    m_OrthoBlend = iam_tween_float(ImHashStr("EditorCamera"), ImHashStr("ortho"), m_Ortho ? 1.0f : 0.0f,
                                   0.3f, iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt,
                                   m_Ortho ? 1.0f : 0.0f);

    // Framing / view-change animation.
    if (m_AnimT < 1.0f)
    {
        m_AnimT = std::min(1.0f, m_AnimT + dt / m_AnimDuration);
        float e = easing ? iam_eval_preset(iam_ease_out_cubic, m_AnimT) : m_AnimT;
        m_Pivot = glm::mix(m_FromPivot, m_ToPivot, e);
        m_Rotation = glm::normalize(glm::slerp(m_FromRot, m_ToRot, e));
        m_Distance = std::exp(glm::mix(std::log(m_FromDist), std::log(m_ToDist), e));
    }

    const bool alt = io.KeyAlt;
    const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    // Begin a drag only when the press happened over the viewport.
    if (m_Drag == DragMode::None && in.hovered)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            m_Drag = alt ? DragMode::Zoom : (m_2DMode ? DragMode::Pan : DragMode::Fly);
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            m_Drag = DragMode::Pan;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !in.gizmoActive)
        {
            if (alt && !m_2DMode) m_Drag = DragMode::Orbit;
            else if (alt || in.handTool) m_Drag = DragMode::Pan;
        }
        if (m_Drag != DragMode::None)
        {
            m_AnimT = 1.0f;
            m_FlyTime = 0.0f;
            m_FlyVelocity = glm::vec3(0.0f);
        }
    }

    // End drags when their button is released.
    switch (m_Drag)
    {
    case DragMode::Fly: if (!rmb) m_Drag = DragMode::None; break;
    case DragMode::Zoom: if (!rmb) m_Drag = DragMode::None; break;
    case DragMode::Orbit: if (!lmb) m_Drag = DragMode::None; break;
    case DragMode::Pan: if (!lmb && !mmb && !rmb) m_Drag = DragMode::None; break;
    default: break;
    }

    const ImVec2 md = io.MouseDelta;
    const float worldPerPixel = 2.0f * m_Distance * std::tan(glm::radians(fov) * 0.5f) / std::max(in.viewportHeight, 1.0f);

    switch (m_Drag)
    {
    case DragMode::Fly:
    {
        glm::vec3 pos = Position();
        if (m_Ortho && !m_2DMode) m_Ortho = false; // Unity leaves iso when flying
        m_Rotation = YawPitch(m_Rotation, -md.x * kLookSensitivity, -md.y * kLookSensitivity);

        glm::vec3 move(0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += Forward();
        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= Forward();
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += Right();
        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= Right();
        if (ImGui::IsKeyDown(ImGuiKey_E)) move += glm::vec3(0, 1, 0);
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= glm::vec3(0, 1, 0);

        if (io.MouseWheel != 0.0f)
        {
            speed = std::clamp(speed * std::pow(1.15f, io.MouseWheel), speedMin, speedMax);
            speedOverlayTimer = 1.5f;
        }

        glm::vec3 target(0.0f);
        if (glm::dot(move, move) > 0.0f)
        {
            m_FlyTime += dt;
            float accel = acceleration ? 1.0f + std::min(m_FlyTime * m_FlyTime * 0.8f, 8.0f) : 1.0f;
            float boost = io.KeyShift ? 4.0f : 1.0f;
            target = glm::normalize(move) * speed * 10.0f * accel * boost;
        }
        else
        {
            m_FlyTime = 0.0f;
        }
        const float blend = easing ? 1.0f - std::exp(-dt * 10.0f) : 1.0f;
        m_FlyVelocity = glm::mix(m_FlyVelocity, target, blend);
        pos += m_FlyVelocity * dt;
        m_Pivot = pos + Forward() * m_Distance;
        break;
    }
    case DragMode::Orbit:
        m_Rotation = YawPitch(m_Rotation, -md.x * kOrbitSensitivity, -md.y * kOrbitSensitivity);
        break;
    case DragMode::Pan:
        m_Pivot += (-Right() * md.x + Up() * md.y) * worldPerPixel;
        break;
    case DragMode::Zoom:
    {
        float delta = std::fabs(md.x) > std::fabs(md.y) ? md.x : -md.y;
        m_Distance = std::clamp(m_Distance * std::exp(-delta * 0.006f * (io.KeyShift ? 3.0f : 1.0f)), 0.01f, 1e5f);
        break;
    }
    default:
        break;
    }

    // Wheel zoom (only when not flying).
    if (m_Drag != DragMode::Fly && in.hovered && io.MouseWheel != 0.0f)
    {
        m_AnimT = 1.0f;
        float factor = std::exp(-io.MouseWheel * (io.KeyShift ? 0.45f : 0.15f));
        m_Distance = std::clamp(m_Distance * factor, 0.01f, 1e5f);
    }

    // Arrow keys move the camera along the ground plane.
    if (m_Drag == DragMode::None && in.allowKeyboard && !io.KeyCtrl && !io.KeyAlt)
    {
        glm::vec3 fwd = Forward();
        fwd.y = 0.0f;
        fwd = glm::dot(fwd, fwd) > 1e-6f ? glm::normalize(fwd) : Up();
        glm::vec3 move(0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) move += m_2DMode ? Up() : fwd;
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) move -= m_2DMode ? Up() : fwd;
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) move += Right();
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) move -= Right();
        if (glm::dot(move, move) > 0.0f)
        {
            m_AnimT = 1.0f;
            m_Pivot += glm::normalize(move) * std::max(m_Distance, 1.0f) * dt * (io.KeyShift ? 3.0f : 1.0f);
        }
    }

    if (speedOverlayTimer > 0.0f) speedOverlayTimer -= dt;
    return m_Drag != DragMode::None;
}
