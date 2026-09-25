#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Scene-view camera modeled on Unity's SceneView camera:
//  - RMB drag: look around (flythrough), + WASD/QE to fly, Shift to go faster, wheel changes speed
//  - Alt+LMB: orbit around pivot      - MMB / Alt+MMB / Hand tool LMB: pan
//  - Alt+RMB drag or wheel: zoom      - F: frame selection (animated)
//  - Arrow keys: move on the ground plane
class EditorCamera
{
public:
    enum class DragMode { None, Fly, Orbit, Pan, Zoom };

    struct Input
    {
        bool hovered = false;      // mouse over the viewport image
        bool allowKeyboard = false; // viewport focused and no text input active
        bool handTool = false;     // Unity "View" tool (Q): LMB pans
        float viewportHeight = 1.0f;
        bool gizmoActive = false;  // don't start LMB actions while a gizmo is hovered/used
    };

    // Returns true when the camera is consuming the mouse (drag in progress).
    bool Update(const Input& in, float dt);

    glm::vec3 Position() const;
    glm::vec3 Forward() const { return m_Rotation * glm::vec3(0, 0, -1); }
    glm::vec3 Right() const { return m_Rotation * glm::vec3(1, 0, 0); }
    glm::vec3 Up() const { return m_Rotation * glm::vec3(0, 1, 0); }
    glm::quat Rotation() const { return m_Rotation; }
    glm::vec3 Pivot() const { return m_Pivot; }
    float Distance() const { return m_Distance; }

    glm::mat4 View() const;
    glm::mat4 Projection(float aspect, bool zeroToOneDepth) const;
    float NearClip() const;
    float FarClip() const;
    // Effective clip range of Projection() (orthographic uses a symmetric range around the eye).
    void ClipRange(float& nearClip, float& farClip) const;

    void SetView(const glm::mat4& view);          // from ImGuizmo::ViewManipulate
    void Frame(const glm::vec3& center, float radius, bool animate = true);
    void LookAlong(const glm::vec3& forward, const glm::vec3& up, bool animate = true);
    void SetState(const glm::vec3& pivot, const glm::quat& rotation, float distance, bool animate);
    void AlignWith(const glm::vec3& position, const glm::quat& rotation); // "Align View to Selected"

    bool Orthographic() const { return m_Ortho; }
    void SetOrthographic(bool ortho);
    float OrthoBlend() const { return m_OrthoBlend; }
    bool Is2DMode() const { return m_2DMode; }
    void Set2DMode(bool enabled);

    DragMode CurrentDrag() const { return m_Drag; }
    bool IsFlying() const { return m_Drag == DragMode::Fly; }
    bool IsAnimating() const { return m_AnimT < 1.0f; }

    // Snapped-view name ("Top", "Front", ...) or nullptr when not axis aligned.
    const char* AxisViewName() const;
    int GridPlane() const; // 0 = XZ, 1 = XY, 2 = YZ

    // Camera settings (Unity's scene camera popup)
    float fov = 60.0f;
    float speed = 1.0f;
    float speedMin = 0.01f;
    float speedMax = 2.0f;
    bool easing = true;
    bool acceleration = true;
    bool dynamicClipping = true;
    float nearClipSetting = 0.03f;
    float farClipSetting = 10000.0f;

    // Transient overlay (e.g. "Camera speed 0.45") shown by the scene view.
    float speedOverlayTimer = 0.0f;

private:
    void StartAnimation(const glm::vec3& pivot, const glm::quat& rotation, float distance, float duration);

    glm::vec3 m_Pivot{ 0.0f, 0.5f, 0.0f };
    glm::quat m_Rotation{ 1, 0, 0, 0 };
    float m_Distance = 12.0f;
    bool m_Ortho = false;
    float m_OrthoBlend = 0.0f;
    bool m_2DMode = false;

    DragMode m_Drag = DragMode::None;
    float m_FlyTime = 0.0f;
    glm::vec3 m_FlyVelocity{ 0.0f };

    // Animation
    float m_AnimT = 1.0f;
    float m_AnimDuration = 0.3f;
    glm::vec3 m_FromPivot{ 0.0f }, m_ToPivot{ 0.0f };
    glm::quat m_FromRot{ 1, 0, 0, 0 }, m_ToRot{ 1, 0, 0, 0 };
    float m_FromDist = 1.0f, m_ToDist = 1.0f;

    // Saved 3D state when switching into 2D mode.
    glm::quat m_Saved3DRot{ 1, 0, 0, 0 };
    bool m_Saved3DOrtho = false;

public:
    EditorCamera();
};
