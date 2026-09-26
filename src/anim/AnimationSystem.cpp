#include "anim/AnimationSystem.h"

#include "core/Log.h"
#include "render/Resources.h"

#include <glm/gtc/quaternion.hpp>

namespace fs = std::filesystem;

AnimatorController* AnimationSystem::Controller(const std::string& path)
{
    if (path.empty()) return nullptr;
    auto it = m_Controllers.find(path);
    if (it != m_Controllers.end()) return it->second.controller.get();
    ControllerEntry entry;
    std::error_code ec;
    entry.stamp = fs::last_write_time(path, ec);
    entry.controller = std::make_unique<AnimatorController>();
    if (!entry.controller->Load(path))
    {
        LOG_WARN("Could not load animator controller %s", path.c_str());
        entry.controller.reset();
    }
    return (m_Controllers[path] = std::move(entry)).controller.get();
}

void AnimationSystem::ControllerEdited(const std::string& path)
{
    auto it = m_Controllers.find(path);
    if (it == m_Controllers.end()) return;
    std::error_code ec;
    it->second.stamp = fs::last_write_time(path, ec);
    it->second.version++;
}

AnimatorInstance* AnimationSystem::Instance(EntityId animatorEntity)
{
    auto it = m_Animators.find(animatorEntity);
    return it != m_Animators.end() && it->second.instance.Valid() ? &it->second.instance : nullptr;
}

const std::vector<glm::mat4>* AnimationSystem::Palette(EntityId meshEntity) const
{
    auto it = m_Palettes.find(meshEntity);
    return it != m_Palettes.end() ? &it->second : nullptr;
}

const MeshData* AnimationSystem::SkinnedMesh(const Entity& e) const
{
    if (!e.meshRenderer.enabled || !m_Res) return nullptr;
    const Mesh* mesh = m_Res->GetMesh(e.meshRenderer.mesh);
    return mesh && mesh->data.Skinned() ? &mesh->data : nullptr;
}

void AnimationSystem::Update(Scene& scene, float dt, bool playing)
{
    // Controllers changed on disk (outside the editor) are reloaded.
    m_CheckTimer += dt;
    if (m_CheckTimer > 1.0f)
    {
        m_CheckTimer = 0.0f;
        for (auto& [path, entry] : m_Controllers)
        {
            std::error_code ec;
            const auto stamp = fs::last_write_time(path, ec);
            if (ec || stamp == entry.stamp) continue;
            entry.stamp = stamp;
            if (!entry.controller) entry.controller = std::make_unique<AnimatorController>();
            if (entry.controller->Load(path)) entry.version++;
        }
    }

    m_Palettes.clear();
    std::unordered_map<EntityId, bool> animated; // skinned meshes driven by an animator this frame

    for (Entity& e : scene.entities)
    {
        if (!e.animator.enabled || !scene.IsActiveInHierarchy(e.id)) continue;
        // The rig: first skinned mesh on the object or its children.
        std::vector<std::pair<EntityId, const MeshData*>> meshes;
        if (const MeshData* md = SkinnedMesh(e)) meshes.push_back({ e.id, md });
        for (const Entity& c : scene.entities)
            if (c.id != e.id && scene.IsAncestor(e.id, c.id) && scene.IsActiveInHierarchy(c.id))
                if (const MeshData* md = SkinnedMesh(c)) meshes.push_back({ c.id, md });
        if (meshes.empty()) continue;
        const Skeleton& skeleton = *meshes[0].second->skeleton;

        Runtime& rt = m_Animators[e.id];
        AnimatorController* controller = Controller(e.animator.controller);
        const uint32_t version = controller ? m_Controllers[e.animator.controller].version : 0;
        if (!controller)
        {
            rt.instance = AnimatorInstance{};
            rt.pose = skeleton.rest;
        }
        else if (rt.controllerPath != e.animator.controller || rt.controllerVersion != version || !rt.instance.Valid())
        {
            // Load every clip the controller uses now, not on the first frame a state plays (avoids hitches).
            for (const AnimState& st : controller->states)
            {
                m_Clips.Get(st.clip);
                for (const BlendChild& ch : st.children) m_Clips.Get(ch.clip);
            }
            rt.controllerPath = e.animator.controller;
            rt.controllerVersion = version;
            rt.instance.Reset(*controller);
        }

        if (controller)
        {
            if (playing)
            {
                RootMotion motion;
                rt.instance.Update(dt, m_Clips, skeleton, e.animator.applyRootMotion, rt.pose, motion);
                if (e.animator.applyRootMotion && (glm::dot(motion.position, motion.position) > 0.0f || motion.yaw != 0.0f))
                {
                    // Root motion moves the object in its own (local) frame, like Unity's Animator.applyRootMotion.
                    Transform& t = e.transform;
                    t.position += t.rotation * (motion.position * t.scale);
                    t.rotation = glm::normalize(t.rotation * glm::angleAxis(motion.yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
                    t.SyncEulerFromRotation();
                }
            }
            else
            {
                rt.instance.Reset(*controller);
                rt.instance.SamplePreview(m_Clips, skeleton, rt.pose);
            }
        }
        if (rt.pose.size() != skeleton.names.size()) rt.pose = skeleton.rest;
        PoseToModel(skeleton, rt.pose, rt.model);

        for (auto& [meshEntity, md] : meshes)
        {
            if (md->skeleton.get() != &skeleton) continue;
            std::vector<glm::mat4>& palette = m_Palettes[meshEntity];
            palette.resize(md->jointBones.size());
            for (size_t j = 0; j < md->jointBones.size(); ++j)
            {
                const int bone = md->jointBones[j];
                palette[j] = (bone >= 0 && bone < static_cast<int>(rt.model.size()) ? rt.model[bone] : glm::mat4(1.0f)) * md->inverseBind[j];
            }
            animated[meshEntity] = true;
        }
    }

    // Skinned meshes without an Animator show their bind pose.
    for (const Entity& e : scene.entities)
    {
        if (animated.count(e.id)) continue;
        const MeshData* md = SkinnedMesh(e);
        if (!md) continue;
        std::vector<glm::mat4> model;
        PoseToModel(*md->skeleton, md->skeleton->rest, model);
        std::vector<glm::mat4>& palette = m_Palettes[e.id];
        palette.resize(md->jointBones.size());
        for (size_t j = 0; j < md->jointBones.size(); ++j)
            palette[j] = (md->jointBones[j] >= 0 ? model[md->jointBones[j]] : glm::mat4(1.0f)) * md->inverseBind[j];
    }

    // Forget animators whose objects are gone.
    for (auto it = m_Animators.begin(); it != m_Animators.end();)
        it = scene.Find(it->first) ? std::next(it) : m_Animators.erase(it);
}
