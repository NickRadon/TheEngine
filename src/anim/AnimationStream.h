#pragma once

#include "anim/Animation.h"

#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

// A writable view of one animator's pose during evaluation. Handles belong to one skeleton and
// become invalid when that skeleton changes. The view itself must never outlive the frame.
struct BoneHandle
{
    const Skeleton* skeleton = nullptr;
    int index = -1;
};

class AnimationStream
{
public:
    AnimationStream(const Skeleton& skeleton, Pose& pose, RootMotion& motion);

    BoneHandle Bind(const std::string& name) const;
    bool Valid(BoneHandle bone) const;
    const Skeleton& Rig() const { return m_Skeleton; }
    BoneTransform Local(BoneHandle bone) const;
    glm::mat4 Model(BoneHandle bone);
    bool SetLocal(BoneHandle bone, const BoneTransform& transform);
    bool SetModel(BoneHandle bone, const glm::mat4& transform);
    RootMotion& Motion() { return m_Motion; }
    const RootMotion& Motion() const { return m_Motion; }
    void SetCurve(std::string name, float value) { m_Curves[std::move(name)] = value; }
    bool Curve(const std::string& name, float& value) const;
    const std::vector<glm::mat4>& ModelPose();

private:
    const Skeleton& m_Skeleton;
    Pose& m_Pose;
    RootMotion& m_Motion;
    std::vector<glm::mat4> m_Model;
    std::unordered_map<std::string, float> m_Curves;
    bool m_Dirty = true;
};
