#include "anim/AnimationSystem.h"
#include "anim/AnimationGraph.h"

#include "core/Log.h"
#include "render/Resources.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <sstream>
#include <exception>
#include <fstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace
{
    // Add the helper transforms authored in a Unity character prefab without changing the FBX's skinned-joint indices.
    std::shared_ptr<Skeleton> LoadHelperRig(const std::string& path, const Skeleton& source, std::vector<RigOperation>& operations)
    {
        std::ifstream in(path);
        std::string magic;
        int version = 0;
        if (!(in >> magic >> version) || magic != "TheEngineRig" || (version != 1 && version != 2))
        {
            LOG_ERROR("Could not load rig setup %s", path.c_str());
            return nullptr;
        }
        auto rig = std::make_shared<Skeleton>(source);
        std::string line;
        std::getline(in, line);
        int added = 0;
        while (std::getline(in, line))
        {
            std::istringstream row(line);
            std::string kind, name, parentName;
            if (!(row >> kind) || kind[0] == '#') continue;
            const bool beforeLook = kind == "precopy";
            if (kind == "copy" || beforeLook || kind == "move" || kind == "rotate" || kind == "addlocalrot" ||
                (version >= 2 && (kind == "modify" || kind == "twobone")))
            {
                if (!(row >> std::quoted(name) >> std::quoted(parentName))) return nullptr;
                RigOperation op;
                op.beforeLook = beforeLook;
                op.target = rig->Find(name);
                op.source = rig->Find(parentName);
                if (op.target < 0 || op.source < 0)
                {
                    LOG_ERROR("Rig operation has unknown bone %s or %s", name.c_str(), parentName.c_str());
                    return nullptr;
                }
                if (kind == "copy" || beforeLook)
                {
                    op.type = RigOperation::Type::Copy;
                    if (version >= 2)
                    {
                        int translation = 1, rotation = 1, scale = 1;
                        if (!(row >> op.weight >> translation >> rotation >> scale)) return nullptr;
                        op.copyTranslation = translation != 0;
                        op.copyRotation = rotation != 0;
                        op.copyScale = scale != 0;
                    }
                }
                else if (kind == "addlocalrot") op.type = RigOperation::Type::AddLocalRotation;
                else if (kind == "move")
                {
                    op.type = RigOperation::Type::Move;
                    if (!(row >> op.position.x >> op.position.y >> op.position.z)) return nullptr;
                }
                else if (kind == "twobone")
                {
                    op.type = RigOperation::Type::TwoBoneIk;
                    std::string hintName;
                    if (!(row >> std::quoted(hintName) >> op.weight)) return nullptr;
                    op.hint = hintName.empty() ? -1 : rig->Find(hintName);
                    if (!hintName.empty() && op.hint < 0) return nullptr;
                    const int middle = rig->parents[op.target];
                    if (middle < 0 || rig->parents[middle] < 0) return nullptr;
                }
                else
                {
                    op.type = kind == "modify" ? RigOperation::Type::Modify : RigOperation::Type::Rotate;
                    float x, y, z, w;
                    if (kind == "modify" && !(row >> op.position.x >> op.position.y >> op.position.z)) return nullptr;
                    if (!(row >> x >> y >> z >> w)) return nullptr;
                    op.rotation = glm::normalize(glm::quat(w, x, y, z));
                    if (kind == "modify" && !(row >> op.weight)) return nullptr;
                }
                operations.push_back(op);
                continue;
            }
            if (kind != "bone" || !(row >> std::quoted(name) >> std::quoted(parentName)))
            {
                LOG_ERROR("Invalid rig setup entry in %s: %s", path.c_str(), line.c_str());
                return nullptr;
            }
            BoneTransform bone;
            float qx, qy, qz, qw;
            if (!(row >> bone.t.x >> bone.t.y >> bone.t.z >> qx >> qy >> qz >> qw))
            {
                LOG_ERROR("Invalid helper transform %s in %s", name.c_str(), path.c_str());
                return nullptr;
            }
            const int parent = rig->Find(parentName);
            if (parent < 0)
            {
                LOG_ERROR("Rig helper %s has missing parent %s", name.c_str(), parentName.c_str());
                return nullptr;
            }
            if (rig->Find(name) >= 0) continue; // a newer FBX may already include this helper
            bone.r = glm::normalize(glm::quat(qw, qx, qy, qz));
            rig->index[name] = static_cast<int>(rig->names.size());
            rig->names.push_back(name);
            rig->parents.push_back(parent);
            rig->rest.push_back(bone);
            ++added;
        }
        LOG_INFO("Loaded rig setup %s (%d helper bones, %zu operations, %zu total)", path.c_str(), added,
                 operations.size(), rig->names.size());
        return rig;
    }
}

void AnimationSystem::Reset()
{
    for (const auto& [id, rt] : m_Animators)
        if (rt.rigSkeleton) m_Clips.ForgetSkeleton(rt.rigSkeleton.get());
    m_Animators.clear();
}

void AnimationSystem::LogIssues(const std::string& path, const AnimatorController& controller)
{
    const std::string name = fs::path(path).filename().string();
    for (const AnimIssue& issue : controller.Validate(&m_Clips, nullptr)) LOG_WARN("Animator Controller %s: %s", name.c_str(), issue.text.c_str());
}

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
    else
        LogIssues(path, *entry.controller);
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

void AnimationSystem::MaskEdited(const std::string& maskPath)
{
    if (maskPath.empty()) return;
    for (auto& [path, entry] : m_Controllers)
    {
        if (!entry.controller) continue;
        bool changed = false;
        for (AnimLayer& l : entry.controller->layers)
            if (l.maskAsset == maskPath && l.RefreshMaskAsset()) changed = true;
        if (changed) entry.version++; // instances rebuild their per layer mask on a version change
    }
}

AnimatorInstance* AnimationSystem::Instance(EntityId animatorEntity)
{
    auto it = m_Animators.find(animatorEntity);
    return it != m_Animators.end() && it->second.instance.Valid() ? &it->second.instance : nullptr;
}

void AnimationSystem::SetLook(EntityId animatorEntity, float pitch, float yaw)
{
    Runtime& rt = m_Animators[animatorEntity];
    rt.lookPitch = pitch;
    rt.lookYaw = yaw;
}

glm::vec3 AnimationSystem::DeltaPosition(EntityId animatorEntity) const
{
    auto it = m_Animators.find(animatorEntity);
    return it != m_Animators.end() ? it->second.deltaPosition : glm::vec3(0.0f);
}

glm::quat AnimationSystem::DeltaRotation(EntityId animatorEntity) const
{
    auto it = m_Animators.find(animatorEntity);
    return it != m_Animators.end() ? it->second.deltaRotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

bool AnimationSystem::BoneModelMatrix(EntityId animatorEntity, const std::string& bone, glm::mat4& out) const
{
    auto it = m_Animators.find(animatorEntity);
    if (it == m_Animators.end() || !it->second.skeleton) return false;
    const int b = it->second.skeleton->Find(bone);
    if (b < 0 || b >= static_cast<int>(it->second.model.size())) return false;
    out = it->second.model[b];
    return true;
}

// Spreads a pitch (around the character's right axis) and yaw (around up) over the look bones, applied in model
// space so each bone turns around its own position and everything below it follows.
void AnimationSystem::ApplyLook(const Entity& e, Runtime& rt, const Skeleton& skeleton)
{
    if (rt.lookPitch == 0.0f && rt.lookYaw == 0.0f) return;
    struct LookBone { int index; float pitchShare; float yawShare; };
    std::vector<LookBone> bones;
    std::stringstream list(e.animator.lookBones);
    std::string name;
    while (std::getline(list, name, ','))
    {
        const size_t a = name.find_first_not_of(' '), b = name.find_last_not_of(' ');
        if (a == std::string::npos) continue;
        name = name.substr(a, b - a + 1);
        float pitchShare = -1.0f, yawShare = -1.0f;
        if (const size_t first = name.find(':'); first != std::string::npos)
        {
            const size_t second = name.find(':', first + 1);
            if (second != std::string::npos)
            {
                try
                {
                    pitchShare = std::stof(name.substr(first + 1, second - first - 1)) / 90.0f;
                    yawShare = std::stof(name.substr(second + 1)) / 90.0f;
                    name.resize(first);
                }
                catch (const std::exception&) { continue; }
            }
        }
        if (const int bone = skeleton.Find(name); bone >= 0) bones.push_back({ bone, pitchShare, yawShare });
    }
    if (bones.empty()) return;
    const float fallbackShare = 1.0f / static_cast<float>(bones.size());
    std::vector<glm::mat4> model;
    PoseToModel(skeleton, rt.pose, model);
    for (const LookBone& entry : bones)
    {
        const int bone = entry.index;
        const float pitch = rt.lookPitch * (entry.pitchShare < 0.0f ? fallbackShare : entry.pitchShare);
        const float yaw = rt.lookYaw * (entry.yawShare < 0.0f ? fallbackShare : entry.yawShare);
        const glm::quat step = glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)) *
                               glm::angleAxis(glm::radians(pitch), glm::vec3(1, 0, 0));
        // local' = parentRot^-1 * step * parentRot * local (a model-space rotation around the bone's pivot).
        const int parent = skeleton.parents[bone];
        const glm::quat parentRot = parent >= 0 ? glm::normalize(glm::quat_cast(glm::mat3(model[parent]))) : glm::quat(1, 0, 0, 0);
        rt.pose[bone].r = glm::normalize(glm::inverse(parentRot) * step * parentRot * rt.pose[bone].r);
        PoseToModel(skeleton, rt.pose, model); // following bones see the change
    }
}

// Bone sockets: direct children of the animated object that follow a bone of its rig.
void AnimationSystem::UpdateSockets(Scene& scene, const Entity& animator, const Runtime& rt)
{
    if (!rt.skeleton) return;
    for (Entity& child : scene.entities)
    {
        if (child.parent != animator.id || !child.boneSocket.enabled) continue;
        const int bone = rt.skeleton->Find(child.boneSocket.bone);
        if (bone < 0 || bone >= static_cast<int>(rt.model.size())) continue;
        const glm::vec3 r = glm::radians(child.boneSocket.euler);
        const glm::mat4 offset = glm::translate(glm::mat4(1.0f), child.boneSocket.position) * glm::eulerAngleYXZ(r.y, r.x, r.z);
        const glm::mat4 local = rt.model[bone] * offset;
        glm::vec3 scale, pos, skew;
        glm::vec4 persp;
        glm::quat rot;
        glm::decompose(local, scale, rot, pos, skew, persp);
        child.transform.position = pos;
        if (child.boneSocket.followRotation)
        {
            child.transform.rotation = glm::normalize(rot);
            child.transform.SyncEulerFromRotation();
        }
    }
}

void AnimationSystem::ApplyRigOperations(Runtime& rt, const Skeleton& skeleton, bool beforeLook)
{
    RootMotion unusedMotion;
    AnimationStream stream(skeleton, rt.pose, unusedMotion);
    const auto rotOf = [](const glm::mat4& matrix) { return glm::normalize(glm::quat_cast(glm::mat3(matrix))); };
    for (const RigOperation& op : rt.rigOperations)
    {
        if (op.beforeLook != beforeLook) continue;
        const BoneHandle target{ &skeleton, op.target }, source{ &skeleton, op.source };
        if (!stream.Valid(target) || !stream.Valid(source)) continue;
        const float weight = glm::clamp(op.weight, 0.0f, 1.0f);
        if (weight <= 0.0f) continue;
        if (op.type == RigOperation::Type::TwoBoneIk)
        {
            const int tip = op.target, middle = skeleton.parents[tip];
            const int upper = middle >= 0 ? skeleton.parents[middle] : -1;
            if (upper < 0 || upper == op.source || middle == op.source) continue;
            const glm::mat4 upperWorld = stream.Model({ &skeleton, upper });
            const glm::mat4 middleWorld = stream.Model({ &skeleton, middle });
            const glm::mat4 tipWorld = stream.Model(target);
            const glm::mat4 goalWorld = stream.Model(source);
            const glm::vec3 a(upperWorld[3]), b(middleWorld[3]), c(tipWorld[3]), goal(goalWorld[3]);
            const float l1 = glm::length(b - a), l2 = glm::length(c - b);
            const glm::vec3 delta = goal - a;
            const float distance = glm::length(delta);
            if (l1 < 1e-5f || l2 < 1e-5f || distance < 1e-5f) continue;
            const glm::vec3 direction = delta / distance;
            const float reach = glm::clamp(distance, std::abs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
            const float along = (l1 * l1 - l2 * l2 + reach * reach) / (2.0f * reach);
            const float height = std::sqrt(std::max(l1 * l1 - along * along, 0.0f));
            glm::vec3 bend;
            if (op.hint >= 0 && stream.Valid({ &skeleton, op.hint }))
            {
                const glm::vec3 pole(stream.Model({ &skeleton, op.hint })[3]);
                bend = pole - a - direction * glm::dot(pole - a, direction);
            }
            else bend = (b - a) - direction * glm::dot(b - a, direction);
            if (glm::dot(bend, bend) < 1e-10f)
            {
                const glm::vec3 axis = std::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                bend = glm::cross(direction, axis);
            }
            bend = glm::normalize(bend);
            const glm::vec3 elbow = a + direction * along + bend * height;
            const glm::vec3 end = a + direction * reach;
            const glm::quat d1 = glm::rotation(glm::normalize(b - a), glm::normalize(elbow - a));
            const glm::quat newUpper = glm::normalize(d1 * rotOf(upperWorld));
            const glm::quat d2 = glm::rotation(glm::normalize(d1 * (c - b)), glm::normalize(end - elbow));
            const glm::quat newMiddle = glm::normalize(d2 * d1 * rotOf(middleWorld));
            const int upperParent = skeleton.parents[upper];
            const glm::quat parentRotation = upperParent >= 0 ? rotOf(stream.Model({ &skeleton, upperParent })) : glm::quat(1, 0, 0, 0);
            BoneTransform upperLocal = stream.Local({ &skeleton, upper });
            BoneTransform middleLocal = stream.Local({ &skeleton, middle });
            BoneTransform tipLocal = stream.Local(target);
            upperLocal.r = glm::slerp(upperLocal.r, glm::normalize(glm::inverse(parentRotation) * newUpper), weight);
            middleLocal.r = glm::slerp(middleLocal.r, glm::normalize(glm::inverse(newUpper) * newMiddle), weight);
            tipLocal.r = glm::slerp(tipLocal.r, glm::normalize(glm::inverse(newMiddle) * rotOf(goalWorld)), weight);
            stream.SetLocal({ &skeleton, upper }, upperLocal);
            stream.SetLocal({ &skeleton, middle }, middleLocal);
            stream.SetLocal(target, tipLocal);
            continue;
        }
        if (op.type == RigOperation::Type::AddLocalRotation)
        {
            BoneTransform local = stream.Local(target);
            local.r = glm::slerp(local.r, glm::normalize(local.r * stream.Local(source).r), weight);
            stream.SetLocal(target, local);
            continue;
        }
        glm::mat4 world = stream.Model(target);
        if (op.type == RigOperation::Type::Copy)
        {
            BoneTransform original = stream.Local(target);
            BoneTransform copied = original;
            const glm::mat4 from = stream.Model(source);
            if (!stream.SetModel(target, from)) continue;
            copied = stream.Local(target);
            copied.t = op.copyTranslation ? glm::mix(original.t, copied.t, weight) : original.t;
            copied.r = op.copyRotation ? glm::slerp(original.r, copied.r, weight) : original.r;
            copied.s = op.copyScale ? glm::mix(original.s, copied.s, weight) : original.s;
            stream.SetLocal(target, copied);
            continue;
        }
        if (op.type == RigOperation::Type::Move || op.type == RigOperation::Type::Modify)
        {
            const glm::quat space = rotOf(stream.Model(source));
            world[3] += glm::vec4(space * op.position * weight, 0.0f);
        }
        if (op.type == RigOperation::Type::Rotate || op.type == RigOperation::Type::Modify)
        {
            const glm::quat space = rotOf(stream.Model(source));
            const glm::quat current = rotOf(world);
            const glm::quat offset = glm::slerp(glm::quat(1, 0, 0, 0), op.rotation, weight);
            const glm::quat rotated = glm::normalize(space * offset * glm::inverse(space) * current);
            const glm::vec3 worldScale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                                       glm::length(glm::vec3(world[2])));
            world = glm::translate(glm::mat4(1.0f), glm::vec3(world[3])) * glm::mat4_cast(rotated) *
                    glm::scale(glm::mat4(1.0f), worldScale);
        }
        stream.SetModel(target, world);
    }
    rt.model = stream.ModelPose();
}

// Two-bone arm IK: reaches the hand for the rig's ik_hand_<side> target bone (first-person rigs animate the
// target, the FK arm only roughly agrees). The elbow keeps the side it bends to, and the hand takes the target's
// rotation through the fixed hand-to-target offset of the bind pose.
void AnimationSystem::SolveHandIk(Runtime& rt, const Skeleton& skeleton)
{
    std::vector<glm::mat4> restModel;
    PoseToModel(skeleton, skeleton.rest, restModel);
    auto rotOf = [](const glm::mat4& m) { return glm::normalize(glm::quat_cast(glm::mat3(m))); };
    for (const char* side : { "r", "l" })
    {
        const int upper = skeleton.Find(std::string("upperarm_") + side), lower = skeleton.Find(std::string("lowerarm_") + side);
        const int hand = skeleton.Find(std::string("hand_") + side), target = skeleton.Find(std::string("ik_hand_") + side);
        if (upper < 0 || lower < 0 || hand < 0 || target < 0) continue;
        const glm::vec3 a = glm::vec3(rt.model[upper][3]), b = glm::vec3(rt.model[lower][3]), c = glm::vec3(rt.model[hand][3]);
        const glm::vec3 t = glm::vec3(rt.model[target][3]);
        const float l1 = glm::length(b - a), l2 = glm::length(c - b);
        glm::vec3 toTarget = t - a;
        float dist = glm::length(toTarget);
        if (l1 < 1e-5f || l2 < 1e-5f || dist < 1e-5f) continue;
        const glm::vec3 dir = toTarget / dist;
        dist = glm::clamp(dist, std::abs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
        const float along = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
        const float height = std::sqrt(std::max(l1 * l1 - along * along, 0.0f));
        glm::vec3 bend = (b - a) - dir * glm::dot(b - a, dir);
        bend = glm::length(bend) > 1e-5f ? glm::normalize(bend) : glm::vec3(0.0f, -1.0f, 0.0f);
        const glm::vec3 elbow = a + dir * along + bend * height;
        const glm::vec3 reach = a + dir * dist;

        const int up = skeleton.parents[upper];
        const glm::quat parentRot = up >= 0 ? rotOf(rt.model[up]) : glm::quat(1, 0, 0, 0);
        const glm::quat upperRot = rotOf(rt.model[upper]), lowerRot = rotOf(rt.model[lower]);
        const glm::quat d1 = glm::rotation(glm::normalize(b - a), glm::normalize(elbow - a));
        const glm::quat newUpper = glm::normalize(d1 * upperRot);
        const glm::quat d2 = glm::rotation(glm::normalize(d1 * (c - b)), glm::normalize(reach - elbow));
        const glm::quat newLower = glm::normalize(d2 * d1 * lowerRot);
        const glm::quat offset = glm::inverse(rotOf(restModel[target])) * rotOf(restModel[hand]);
        const glm::quat newHand = glm::normalize(rotOf(rt.model[target]) * offset);
        float weight = 1.0f;
        const std::string weightParam = std::string("IkHand_") + side;
        if (rt.instance.Controller() && rt.instance.Controller()->FindParam(weightParam) >= 0) weight = glm::clamp(rt.instance.GetParam(weightParam), 0.0f, 1.0f);
        if (weight <= 0.0f) continue;
        const glm::quat oldUpper = rt.pose[upper].r, oldLower = rt.pose[lower].r, oldHand = rt.pose[hand].r;
        rt.pose[upper].r = glm::normalize(glm::inverse(parentRot) * newUpper);
        rt.pose[lower].r = glm::normalize(glm::inverse(newUpper) * newLower);
        rt.pose[hand].r = glm::normalize(glm::inverse(newLower) * newHand);
        rt.pose[upper].r = glm::slerp(oldUpper, rt.pose[upper].r, weight);
        rt.pose[lower].r = glm::slerp(oldLower, rt.pose[lower].r, weight);
        rt.pose[hand].r = glm::slerp(oldHand, rt.pose[hand].r, weight);
    }
}

const Skeleton* AnimationSystem::SkeletonOf(EntityId animatorEntity) const
{
    auto it = m_Animators.find(animatorEntity);
    return it != m_Animators.end() ? it->second.skeleton : nullptr;
}

// Dynamic Bones: a verlet spring per bone in world space, pulled toward the animated pose. The result is written
// back as bone rotations (each bone aims at its simulated child), so skinning, sockets and children follow.
void AnimationSystem::SimulateDynamicBones(const Entity& e, const glm::mat4& world, float dt, Runtime& rt, const Skeleton& skeleton)
{
    const glm::vec3 origin = glm::vec3(world[3]);
    const glm::vec3 objectMove = rt.hasLastOrigin ? origin - rt.lastOrigin : glm::vec3(0.0f);
    rt.lastOrigin = origin;
    rt.hasLastOrigin = true;
    const auto& chains = e.dynamicBones.chains;
    if (!e.dynamicBones.enabled || chains.empty() || dt <= 0.0f)
    {
        rt.dynChains.clear();
        return;
    }
    // (Re)build the particle lists when the chain setup changed.
    bool rebuild = rt.dynChains.size() != chains.size();
    for (size_t c = 0; !rebuild && c < chains.size(); ++c) rebuild = rt.dynChains[c].root != chains[c].root;
    if (rebuild)
    {
        rt.dynChains.clear();
        for (const DynamicBoneChain& c : chains)
        {
            Runtime::DynChain chain;
            chain.root = c.root;
            const int root = skeleton.Find(c.root);
            if (root >= 0)
                for (int i = root; i < static_cast<int>(skeleton.names.size()); ++i)
                {
                    int p = i == root ? root : skeleton.parents[i];
                    while (p >= 0 && p != root) p = skeleton.parents[p];
                    if (i == root || p == root) chain.bones.push_back(i);
                }
            rt.dynChains.push_back(std::move(chain));
        }
    }

    const glm::mat4 inv = glm::inverse(world);
    std::vector<glm::quat> newRot(skeleton.names.size());
    for (size_t c = 0; c < chains.size(); ++c)
    {
        const DynamicBoneChain& cfg = chains[c];
        Runtime::DynChain& chain = rt.dynChains[c];
        const size_t n = chain.bones.size();
        if (n < 2) continue;
        auto animWorld = [&](int bone) { return glm::vec3(world * glm::vec4(glm::vec3(rt.model[bone][3]), 1.0f)); };
        if (chain.pos.size() != n)
        {
            chain.pos.resize(n);
            chain.prev.resize(n);
            for (size_t i = 0; i < n; ++i) chain.pos[i] = chain.prev[i] = animWorld(chain.bones[i]);
        }
        std::unordered_map<int, size_t> slot;
        for (size_t i = 0; i < n; ++i) slot[chain.bones[i]] = i;

        for (size_t i = 0; i < n; ++i)
        {
            const int bone = chain.bones[i];
            const glm::vec3 rest = animWorld(bone);
            if (i == 0)
            {
                chain.prev[0] = chain.pos[0];
                chain.pos[0] = rest;
                continue;
            }
            const size_t parent = slot[skeleton.parents[bone]];
            const glm::vec3 restParent = animWorld(chain.bones[parent]);
            const float restLen = glm::length(rest - restParent);
            glm::vec3& p = chain.pos[i];
            const glm::vec3 v = p - chain.prev[i];
            chain.prev[i] = p + objectMove * cfg.inertia;
            p += v * (1.0f - cfg.damping) + cfg.gravity * dt * dt + objectMove * cfg.inertia;
            // Pull toward the animated pose, and keep within the stiffness range of it.
            glm::vec3 d = rest - p;
            p += d * cfg.elasticity;
            if (cfg.stiffness > 0.0f)
            {
                d = rest - p;
                const float len = glm::length(d), maxLen = restLen * (1.0f - cfg.stiffness) * 2.0f;
                if (len > maxLen && len > 1e-6f) p += d * ((len - maxLen) / len);
            }
            // Bone length.
            glm::vec3 dir = p - chain.pos[parent];
            const float dl = glm::length(dir);
            if (dl > 1e-6f) p = chain.pos[parent] + dir * (restLen / dl);
        }

        // Rotate every bone with a simulated child so it points at it.
        std::vector<int> firstChild(skeleton.names.size(), -1);
        for (size_t i = 1; i < n; ++i)
        {
            const int parentBone = skeleton.parents[chain.bones[i]];
            if (firstChild[parentBone] < 0) firstChild[parentBone] = static_cast<int>(i);
        }
        for (size_t i = 0; i < n; ++i)
        {
            const int bone = chain.bones[i];
            const int parentBone = skeleton.parents[bone];
            const glm::quat parentRot = i == 0 ? (parentBone >= 0 ? glm::quat_cast(glm::mat3(rt.model[parentBone])) : glm::quat(1, 0, 0, 0))
                                               : newRot[parentBone];
            glm::quat rot = parentRot * rt.pose[bone].r;
            if (firstChild[bone] >= 0)
            {
                const size_t child = static_cast<size_t>(firstChild[bone]);
                const glm::vec3 wanted = glm::vec3(inv * glm::vec4(chain.pos[child], 1.0f)) - glm::vec3(inv * glm::vec4(chain.pos[i], 1.0f));
                const glm::vec3 restDir = rot * rt.pose[chain.bones[child]].t;
                if (glm::length(wanted) > 1e-6f && glm::length(restDir) > 1e-6f)
                    rot = glm::normalize(glm::rotation(glm::normalize(restDir), glm::normalize(wanted)) * rot);
            }
            newRot[bone] = rot;
            rt.pose[bone].r = glm::normalize(glm::inverse(parentRot) * rot);
        }
    }
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
            if (!ec && stamp != entry.stamp)
            {
                entry.stamp = stamp;
                if (!entry.controller) entry.controller = std::make_unique<AnimatorController>();
                if (entry.controller->Load(path))
                {
                    entry.version++;
                    LogIssues(path, *entry.controller); // changed outside the editor: report problems once
                }
            }
            // Blend mask assets can change on disk too (the mask window, or an external editor).
            if (entry.controller)
                for (AnimLayer& l : entry.controller->layers)
                    if (!l.maskAsset.empty() && l.RefreshMaskAsset()) entry.version++;
        }
    }

    m_Palettes.clear();
    std::unordered_map<EntityId, bool> animated; // skinned meshes driven by an animator this frame

    // By id: OnAnimatorMove runs scripts, which may add objects (the entity vector can reallocate).
    std::vector<EntityId> animatorIds;
    for (const Entity& x : scene.entities)
        if (x.animator.enabled) animatorIds.push_back(x.id);
    for (const EntityId animId : animatorIds)
    {
        if (!scene.Find(animId)) continue;
        Entity& e = *scene.Find(animId);
        if (!e.animator.enabled || !scene.IsActiveInHierarchy(e.id)) continue;
        // The rig: first skinned mesh on the object or its children.
        std::vector<std::pair<EntityId, const MeshData*>> meshes;
        if (const MeshData* md = SkinnedMesh(e)) meshes.push_back({ e.id, md });
        for (const Entity& c : scene.entities)
            if (c.id != e.id && scene.IsAncestor(e.id, c.id) && scene.IsActiveInHierarchy(c.id))
                if (const MeshData* md = SkinnedMesh(c)) meshes.push_back({ c.id, md });
        if (meshes.empty()) continue;
        const Skeleton& sourceSkeleton = *meshes[0].second->skeleton;
        Runtime& rt = m_Animators[e.id];
        std::error_code rigError;
        const auto rigStamp = e.animator.rig.empty() ? fs::file_time_type{} : fs::last_write_time(e.animator.rig, rigError);
        if (rt.sourceSkeleton != &sourceSkeleton || rt.rigPath != e.animator.rig || rt.rigStamp != rigStamp)
        {
            if (rt.rigSkeleton) m_Clips.ForgetSkeleton(rt.rigSkeleton.get());
            rt.rigOperations.clear();
            rt.rigSkeleton = e.animator.rig.empty() ? nullptr : LoadHelperRig(e.animator.rig, sourceSkeleton, rt.rigOperations);
            if (!rt.rigSkeleton) rt.rigOperations.clear();
            rt.sourceSkeleton = &sourceSkeleton;
            rt.rigPath = e.animator.rig;
            rt.rigStamp = rigStamp;
            rt.instance = AnimatorInstance{}; // cached masks and regions belong to the old skeleton
        }
        const Skeleton& skeleton = rt.rigSkeleton ? *rt.rigSkeleton : sourceSkeleton;
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
            for (const AnimLayer& layer : controller->layers)
                for (const AnimState& st : layer.states)
                {
                    m_Clips.Get(st.clip);
                    for (const BlendChild& ch : st.children) m_Clips.Get(ch.clip);
                }
            rt.controllerPath = e.animator.controller;
            rt.controllerVersion = version;
            rt.instance.Reset(*controller);
        }

        rt.skeleton = &skeleton;
        if (controller)
        {
            if (!e.animator.active)
            {
                // Disabled in the Inspector: the state machine stops and the current pose is kept
                // (rest pose until the animator has been evaluated once).
                if (rt.pose.size() != skeleton.names.size()) rt.pose = skeleton.rest;
            }
            else if (playing)
            {
                RootMotion motion;
                AnimationGraph graph;
                auto output = graph.AddController(rt.instance);
                if (onAnimationStream)
                    output = graph.AddJob(output, [this, id = e.id](AnimationStream& stream) { onAnimationStream(id, stream); });
                graph.SetOutput(output);
                if (!graph.Evaluate(dt, m_Clips, skeleton, true, true, rt.pose, motion)) continue;
                // Root motion in world space (Animator.deltaPosition / deltaRotation).
                const glm::mat4 world = scene.WorldMatrix(e.id);
                rt.deltaPosition = glm::vec3(world * glm::vec4(motion.position, 0.0f));
                rt.deltaRotation = glm::angleAxis(motion.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                const EntityId id = e.id;
                const bool handled = onAnimatorMove && onAnimatorMove(id);
                Entity* self = scene.Find(id); // a script may have changed the scene
                if (!self) continue;
                if (!handled && self->animator.applyRootMotion && (glm::dot(motion.position, motion.position) > 0.0f || motion.yaw != 0.0f))
                {
                    // Root motion moves the object in its own (local) frame, like Unity's Animator.applyRootMotion.
                    Transform& t = self->transform;
                    t.position += t.rotation * (motion.position * t.scale);
                    t.rotation = glm::normalize(t.rotation * glm::angleAxis(motion.yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
                    t.SyncEulerFromRotation();
                }
            }
            else
            {
                rt.instance.Reset(*controller);
                AnimationGraph graph;
                auto output = graph.AddController(rt.instance);
                if (onAnimationStream)
                    output = graph.AddJob(output, [this, id = e.id](AnimationStream& stream) { onAnimationStream(id, stream); });
                graph.SetOutput(output);
                RootMotion ignored;
                if (!graph.Evaluate(0.0f, m_Clips, skeleton, false, false, rt.pose, ignored)) continue;
            }
        }
        if (rt.pose.size() != skeleton.names.size()) rt.pose = skeleton.rest;
        if (!scene.Find(animId)) continue;
        PoseToModel(skeleton, rt.pose, rt.model);
        ApplyRigOperations(rt, skeleton, true);
        ApplyLook(*scene.Find(animId), rt, skeleton);
        PoseToModel(skeleton, rt.pose, rt.model);
        // Ordered rig nodes run before the legacy automatic hand IK. Copy targets therefore see the
        // authored animation, while subsequent Two Bone IK nodes can use those copied targets.
        ApplyRigOperations(rt, skeleton, false);
        if (scene.Find(animId)->animator.handIk)
        {
            SolveHandIk(rt, skeleton);
            PoseToModel(skeleton, rt.pose, rt.model);
        }
        if (playing && scene.Find(animId)->dynamicBones.enabled)
        {
            SimulateDynamicBones(*scene.Find(animId), scene.WorldMatrix(animId), dt, rt, skeleton);
            PoseToModel(skeleton, rt.pose, rt.model);
        }
        UpdateSockets(scene, *scene.Find(animId), rt);

        for (auto& [meshEntity, md] : meshes)
        {
            if (md->skeleton.get() != &sourceSkeleton) continue;
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
