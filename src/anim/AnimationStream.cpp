#include "anim/AnimationStream.h"

#include <glm/gtx/matrix_decompose.hpp>

AnimationStream::AnimationStream(const Skeleton& skeleton, Pose& pose, RootMotion& motion)
    : m_Skeleton(skeleton), m_Pose(pose), m_Motion(motion)
{
}

BoneHandle AnimationStream::Bind(const std::string& name) const
{
    return { &m_Skeleton, m_Skeleton.Find(name) };
}

bool AnimationStream::Valid(BoneHandle bone) const
{
    return bone.skeleton == &m_Skeleton && bone.index >= 0 && bone.index < static_cast<int>(m_Pose.size());
}

BoneTransform AnimationStream::Local(BoneHandle bone) const
{
    return Valid(bone) ? m_Pose[bone.index] : BoneTransform{};
}

const std::vector<glm::mat4>& AnimationStream::ModelPose()
{
    if (m_Dirty)
    {
        PoseToModel(m_Skeleton, m_Pose, m_Model);
        m_Dirty = false;
    }
    return m_Model;
}

glm::mat4 AnimationStream::Model(BoneHandle bone)
{
    return Valid(bone) ? ModelPose()[bone.index] : glm::mat4(1.0f);
}

bool AnimationStream::SetLocal(BoneHandle bone, const BoneTransform& transform)
{
    if (!Valid(bone)) return false;
    m_Pose[bone.index] = transform;
    m_Pose[bone.index].r = glm::normalize(m_Pose[bone.index].r);
    m_Dirty = true;
    return true;
}

bool AnimationStream::SetModel(BoneHandle bone, const glm::mat4& transform)
{
    if (!Valid(bone)) return false;
    const int parent = m_Skeleton.parents[bone.index];
    const glm::mat4 local = parent >= 0 ? glm::inverse(ModelPose()[parent]) * transform : transform;
    BoneTransform result;
    glm::vec3 skew;
    glm::vec4 perspective;
    if (!glm::decompose(local, result.s, result.r, result.t, skew, perspective)) return false;
    result.r = glm::normalize(result.r);
    return SetLocal(bone, result);
}

bool AnimationStream::Curve(const std::string& name, float& value) const
{
    const auto it = m_Curves.find(name);
    if (it == m_Curves.end()) return false;
    value = it->second;
    return true;
}
