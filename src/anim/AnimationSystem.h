#pragma once

#include "anim/AnimatorController.h"
#include "scene/Scene.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ResourceCache;

// Drives Animator components: evaluates their controllers, applies root motion (play mode) and produces the
// skinning matrices the renderer uses for skinned meshes.
class AnimationSystem
{
public:
    void Init(ResourceCache* resources) { m_Res = resources; }

    // playing: advance state machines and apply root motion; otherwise show the default state's first frame.
    void Update(Scene& scene, float dt, bool playing);
    void Reset() { m_Animators.clear(); } // play mode start/end: fresh parameters and states

    // Joint matrices (geometry -> model space of the mesh's object) for a skinned mesh entity.
    const std::vector<glm::mat4>* Palette(EntityId meshEntity) const;

    AnimatorInstance* Instance(EntityId animatorEntity);
    AnimatorController* Controller(const std::string& path); // cached; reloaded when the file changes
    void ControllerEdited(const std::string& path);           // the editor changed it in memory (and saved)
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
    };
    const MeshData* SkinnedMesh(const Entity& e) const;

    ResourceCache* m_Res = nullptr;
    ClipLibrary m_Clips;
    std::unordered_map<std::string, ControllerEntry> m_Controllers;
    std::unordered_map<EntityId, Runtime> m_Animators;
    std::unordered_map<EntityId, std::vector<glm::mat4>> m_Palettes;
    float m_CheckTimer = 0.0f;
};
