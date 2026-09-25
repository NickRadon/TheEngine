#pragma once

#include "render/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

using EntityId = uint32_t;
constexpr EntityId kNullEntity = 0;

struct Transform
{
    glm::vec3 position{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f };
    glm::vec3 euler{ 0.0f }; // degrees; kept alongside the quaternion so the inspector stays stable

    glm::mat4 Matrix() const;
    void SetEuler(const glm::vec3& degrees);
    void SyncEulerFromRotation();
};

// Mesh Filter + Mesh Renderer.
struct MeshRendererComponent
{
    bool enabled = false;
    std::string mesh = "Cube";   // built-in primitive name, or "Assets/.../model.glb#<mesh index>"
    std::string material;        // path to a .mat asset; empty = use the inline properties below
    glm::vec3 color{ 0.8f };
    float metallic = 0.0f;
    float smoothness = 0.5f;
    bool castShadows = true;
};

enum class LightType : int { Directional = 0, Point, Spot };

// The first active directional light drives the procedural sky's sun and casts cascaded shadows.
struct LightComponent
{
    bool enabled = false;
    LightType type = LightType::Directional;
    glm::vec3 color{ 1.0f, 0.957f, 0.839f };
    float intensity = 1.0f;
    bool castShadows = true;     // Unity: Shadow Type (No Shadows / Soft Shadows)
    float shadowStrength = 1.0f;
    float range = 10.0f;          // point / spot
    float spotAngle = 30.0f;      // outer cone angle in degrees (full angle, like Unity)
    float innerSpotAngle = 21.8f; // inner cone angle in degrees
};

struct CameraComponent
{
    bool enabled = false;
    float fov = 60.0f;
    float nearClip = 0.3f;
    float farClip = 1000.0f;
    bool orthographic = false;
    float orthoSize = 5.0f;
};

// A C# script attached to an entity (Unity: a MonoBehaviour component). Public field values are stored as
// text so they survive script recompiles; the scripting layer converts them to/from managed values.
struct ScriptField
{
    std::string name;
    std::string type;  // float, int, bool, string, Vector3, Color
    std::string value;
};

struct ScriptComponent
{
    std::string className;
    bool enabled = true;
    std::vector<ScriptField> fields;

    ScriptField* FindField(const std::string& fieldName);
};

struct Entity
{
    EntityId id = kNullEntity;
    EntityId parent = kNullEntity;
    std::string name;
    bool active = true;
    Transform transform;
    MeshRendererComponent meshRenderer;
    LightComponent light;
    CameraComponent camera;
    std::vector<ScriptComponent> scripts;
};

// Per-scene environment: procedural skybox (modeled after Unity's Skybox/Procedural, plus clouds and stars).
struct SkySettings
{
    bool enabled = true;
    float sunSize = 0.04f;
    float sunConvergence = 5.0f;
    float atmosphereThickness = 1.0f;
    glm::vec3 skyTint{ 0.5f };
    glm::vec3 groundColor{ 0.369f, 0.349f, 0.341f };
    float exposure = 1.3f;
    float ambientIntensity = 1.0f;
    float cloudCoverage = 0.45f;
    float cloudDensity = 0.85f;
    float cloudSpeed = 1.0f;
    float cloudScale = 0.6f;
    float stars = 1.0f;
    glm::vec3 fallbackColor{ 0.19f, 0.3f, 0.47f }; // solid background when the skybox is disabled
    float shadowDistance = 80.0f;                     // cascaded shadows cover this distance from the camera
    bool ssao = true;                                 // screen-space ambient occlusion
    float ssaoRadius = 1.0f;
    float ssaoIntensity = 2.0f;
};

class Scene
{
public:
    std::string name = "SampleScene";
    std::vector<Entity> entities; // order defines sibling order in the hierarchy
    SkySettings sky;

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    Entity& Create(const std::string& name, EntityId parent = kNullEntity);
    Entity* Find(EntityId id);
    const Entity* Find(EntityId id) const;
    int IndexOf(EntityId id) const;

    glm::mat4 WorldMatrix(EntityId id) const;
    void SetWorldMatrix(EntityId id, const glm::mat4& world);
    bool IsActiveInHierarchy(EntityId id) const;
    bool IsAncestor(EntityId ancestor, EntityId id) const;
    std::vector<EntityId> Children(EntityId parent) const;

    void SetParent(EntityId child, EntityId parent, bool keepWorld = true);
    void MoveBefore(EntityId id, EntityId before); // reorder among siblings
    void Destroy(EntityId id);                      // destroys children too
    EntityId Duplicate(EntityId id);                // deep copy, returns new root

    void CreateDefault(); // Main Camera + Directional Light + a few primitives

    EntityId NextId() const { return m_NextId; }

private:
    EntityId CloneRecursive(EntityId src, EntityId newParent, int insertAt);
    EntityId m_NextId = 1;
};

std::string UniqueName(const Scene& scene, const std::string& base);
