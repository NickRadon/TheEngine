#pragma once

#include "scene/Scene.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct MeshData;

struct RaycastHit
{
    EntityId entity = kNullEntity;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    float distance = 0.0f;
};

enum class CollisionEventType : int { CollisionEnter = 0, CollisionExit, TriggerEnter, TriggerExit };

struct CollisionEvent
{
    CollisionEventType type = CollisionEventType::CollisionEnter;
    EntityId a = kNullEntity, b = kNullEntity;
    glm::vec3 point{ 0.0f };
    glm::vec3 normal{ 0.0f };           // from a towards b
    glm::vec3 relativeVelocity{ 0.0f }; // velocity of b relative to a
};

// Unity ForceMode values.
enum class ForceMode : int { Force = 0, Impulse = 1, VelocityChange = 2, Acceleration = 5 };

// Rigid body simulation for play mode (Jolt Physics). Bodies are created from Rigidbody/Collider components and
// kept in sync every fixed step: new/changed components are (re)built, destroyed entities are removed, and
// transforms edited by scripts or the inspector teleport the body.
class PhysicsWorld
{
public:
    using MeshProvider = std::function<const MeshData*(const std::string& ref)>;

    static void GlobalInit();
    static void GlobalShutdown();

    PhysicsWorld();
    ~PhysicsWorld();

    void Begin(Scene* scene, MeshProvider meshes);
    void End();
    bool Running() const;

    // Advances in fixed steps (Unity's default 0.02 s). Before each step fixedUpdate runs (scripts' FixedUpdate);
    // after it, onEvents receives that step's collision/trigger enter/exit events.
    void Update(float dt, const std::function<void()>& fixedUpdate, const std::function<void(const std::vector<CollisionEvent>&)>& onEvents);

    bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit) const;

    // Rigidbody runtime state (dynamic/kinematic bodies). The body is created on demand.
    glm::vec3 GetVelocity(EntityId id);
    void SetVelocity(EntityId id, const glm::vec3& v);
    glm::vec3 GetAngularVelocity(EntityId id);
    void SetAngularVelocity(EntityId id, const glm::vec3& v);
    void AddForce(EntityId id, const glm::vec3& force, ForceMode mode);
    void AddTorque(EntityId id, const glm::vec3& torque, ForceMode mode);

    glm::vec3 gravity{ 0.0f, -9.81f, 0.0f };
    float fixedDeltaTime = 0.02f;
    int BodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
