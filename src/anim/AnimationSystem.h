#pragma once

#include "anim/AnimatorController.h"
#include "anim/AnimationStream.h"
#include "scene/Scene.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ResourceCache;

struct RigOperation
{
    enum class Type { Copy, Move, Rotate, AddLocalRotation, Modify, TwoBoneIk } type = Type::Copy;
    int target = -1;
    int source = -1; // source bone for Copy, reference space for Move/Rotate
    int hint = -1;   // optional pole target for TwoBoneIk
    float weight = 1.0f;
    bool copyTranslation = true, copyRotation = true, copyScale = true;
    bool beforeLook = false;
    glm::vec3 position{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
};

// Drives Animator components: evaluates their controllers, applies root motion (play mode) and produces the
// skinning matrices the renderer uses for skinned meshes.
class AnimationSystem
{
public:
    void Init(ResourceCache* resources) { m_Res = resources; }

    // playing: advance state machines and apply root motion; otherwise show the default state's first frame.
    void Update(Scene& scene, float dt, bool playing);
    void Reset(); // play mode start/end: fresh parameters and states

    // Joint matrices (geometry -> model space of the mesh's object) for a skinned mesh entity.
    const std::vector<glm::mat4>* Palette(EntityId meshEntity) const;

    AnimatorInstance* Instance(EntityId animatorEntity);
    // The rig of an animated object (null until the animator has been evaluated once).
    const Skeleton* SkeletonOf(EntityId animatorEntity) const;

    // Look modifier (Animator.SetLookAngles): degrees, applied over AnimatorComponent::lookBones.
    void SetLook(EntityId animatorEntity, float pitch, float yaw);
    // Root motion of the last update in world space (Animator.deltaPosition / deltaRotation).
    glm::vec3 DeltaPosition(EntityId animatorEntity) const;
    glm::quat DeltaRotation(EntityId animatorEntity) const;
    // Model-space transform of a bone of the animator's rig (identity if unknown).
    bool BoneModelMatrix(EntityId animatorEntity, const std::string& bone, glm::mat4& out) const;
    // Called after each animator is evaluated in play mode; returns true when a script handled the root motion
    // (Unity's OnAnimatorMove), in which case it isn't applied automatically.
    std::function<bool(EntityId)> onAnimatorMove;
    // Runs on the writable pose before rig constraints. Handles must not be retained beyond this call.
    std::function<void(EntityId, AnimationStream&)> onAnimationStream;
    AnimatorController* Controller(const std::string& path); // cached; reloaded when the file changes
    void ControllerEdited(const std::string& path);           // the editor changed it in memory (and saved)
    // A blend mask asset was saved: re-reads it in every cached controller that references it.
    void MaskEdited(const std::string& maskPath);
    // Logs the problems a controller has (missing clips, unknown parameters, broken transitions).
    void LogIssues(const std::string& path, const AnimatorController& controller);
    ClipLibrary& Clips() { return m_Clips; }

private:
    struct ControllerEntry
    {
        std::unique_ptr<AnimatorController> controller;
        std::filesystem::file_time_type stamp{};
        uint32_t version = 0;
    };
    struct Runtime
    {
        std::string controllerPath;
        uint32_t controllerVersion = ~0u;
        AnimatorInstance instance;
        Pose pose;
        std::vector<glm::mat4> model;
        const Skeleton* skeleton = nullptr;
        const Skeleton* sourceSkeleton = nullptr;
        std::shared_ptr<Skeleton> rigSkeleton;
        std::vector<RigOperation> rigOperations;
        std::string rigPath;
        std::filesystem::file_time_type rigStamp{};
        float lookPitch = 0.0f, lookYaw = 0.0f;
        struct DynChain
        {
            std::string root;
            std::vector<int> bones;          // the root bone and its descendants, parents first
            std::vector<glm::vec3> pos, prev; // world space
        };
        std::vector<DynChain> dynChains;
        glm::vec3 lastOrigin{ 0.0f };
        bool hasLastOrigin = false;
        glm::vec3 deltaPosition{ 0.0f };
        glm::quat deltaRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    };
    void ApplyLook(const Entity& e, Runtime& rt, const Skeleton& skeleton);
    void SolveHandIk(Runtime& rt, const Skeleton& skeleton);
    void SimulateDynamicBones(const Entity& e, const glm::mat4& world, float dt, Runtime& rt, const Skeleton& skeleton);
    void UpdateSockets(Scene& scene, const Entity& animator, const Runtime& rt);
    void ApplyRigOperations(Runtime& rt, const Skeleton& skeleton, bool beforeLook);
    const MeshData* SkinnedMesh(const Entity& e) const;

    ResourceCache* m_Res = nullptr;
    ClipLibrary m_Clips;
    std::unordered_map<std::string, ControllerEntry> m_Controllers;
    std::unordered_map<EntityId, Runtime> m_Animators;
    std::unordered_map<EntityId, std::vector<glm::mat4>> m_Palettes;
    float m_CheckTimer = 0.0f;
};
