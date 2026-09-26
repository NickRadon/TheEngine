#include "anim/Animation.h"

#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

BoneTransform Blend(const BoneTransform& a, const BoneTransform& b, float t)
{
    BoneTransform r;
    r.t = glm::mix(a.t, b.t, t);
    r.r = glm::slerp(a.r, glm::dot(a.r, b.r) < 0.0f ? -b.r : b.r, t);
    r.s = glm::mix(a.s, b.s, t);
    return r;
}

glm::mat4 ToMatrix(const BoneTransform& x)
{
    glm::mat4 m = glm::mat4_cast(x.r);
    m[0] *= x.s.x;
    m[1] *= x.s.y;
    m[2] *= x.s.z;
    m[3] = glm::vec4(x.t, 1.0f);
    return m;
}

void PoseToModel(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& model)
{
    model.resize(pose.size());
    for (size_t i = 0; i < pose.size(); ++i)
    {
        const glm::mat4 local = ToMatrix(pose[i]);
        const int parent = skeleton.parents[i];
        model[i] = parent >= 0 ? model[parent] * local : local;
    }
}

BoneTransform AnimationClip::SampleTrack(int track, float time) const
{
    const Track& tr = tracks[track];
    if (tr.frames.empty()) return tr.rest;
    const float f = std::clamp(time * fps, 0.0f, static_cast<float>(frameCount - 1));
    const int i0 = static_cast<int>(f);
    const int i1 = std::min(i0 + 1, frameCount - 1);
    return Blend(tr.frames[i0], tr.frames[i1], f - static_cast<float>(i0));
}

void AnimationClip::SampleRoot(float time, glm::vec2& xz, float& yaw) const
{
    if (rootXZ.empty())
    {
        xz = glm::vec2(0.0f);
        yaw = 0.0f;
        return;
    }
    const float f = std::clamp(time * fps, 0.0f, static_cast<float>(frameCount - 1));
    const int i0 = static_cast<int>(f);
    const int i1 = std::min(i0 + 1, frameCount - 1);
    const float t = f - static_cast<float>(i0);
    xz = glm::mix(rootXZ[i0], rootXZ[i1], t);
    yaw = glm::mix(rootYaw[i0], rootYaw[i1], t);
}

ClipBinding BindClip(const AnimationClip& clip, const Skeleton& skeleton)
{
    ClipBinding b;
    b.trackForBone.resize(skeleton.names.size(), -1);
    for (size_t i = 0; i < skeleton.names.size(); ++i)
    {
        auto it = clip.trackIndex.find(skeleton.names[i]);
        if (it != clip.trackIndex.end()) b.trackForBone[i] = it->second;
    }
    // Rigs with different proportions: scale the hip translation by the hip height ratio.
    if (skeleton.pelvis >= 0 && b.trackForBone[skeleton.pelvis] >= 0)
    {
        const float target = glm::length(skeleton.rest[skeleton.pelvis].t);
        const float source = glm::length(clip.tracks[b.trackForBone[skeleton.pelvis]].rest.t);
        if (source > 1e-4f && target > 1e-4f) b.pelvisScale = target / source;
    }
    return b;
}

namespace
{
    glm::quat YawRotation(float yaw) { return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)); }
}

void SampleClip(const AnimationClip& clip, const ClipBinding& binding, const Skeleton& skeleton, float time, bool extractRootMotion, Pose& out)
{
    out.resize(skeleton.names.size());
    for (size_t i = 0; i < skeleton.names.size(); ++i)
    {
        const int track = binding.trackForBone[i];
        if (track < 0)
        {
            out[i] = skeleton.rest[i];
            continue;
        }
        const BoneTransform sampled = clip.SampleTrack(track, time);
        BoneTransform& x = out[i];
        // Retarget: rotations come from the clip; translations only for the root and hips (the rest keep the
        // target's bone lengths), like Unreal's skeleton-based retargeting.
        x.r = sampled.r;
        x.s = skeleton.rest[i].s;
        const bool isRoot = static_cast<int>(i) == skeleton.root;
        const bool isPelvis = static_cast<int>(i) == skeleton.pelvis;
        x.t = isRoot ? sampled.t : isPelvis ? sampled.t * binding.pelvisScale : skeleton.rest[i].t;
    }

    if (extractRootMotion && skeleton.root >= 0 && binding.trackForBone[skeleton.root] >= 0 && clip.hasRootMotion)
    {
        // Remove the planar motion since the first frame; the entity moves instead.
        glm::vec2 xz, xz0;
        float yaw, yaw0;
        clip.SampleRoot(time, xz, yaw);
        clip.SampleRoot(0.0f, xz0, yaw0);
        const glm::quat undo = YawRotation(-(yaw - yaw0));
        BoneTransform& root = out[skeleton.root];
        root.t = undo * (root.t - glm::vec3(xz.x - xz0.x, 0.0f, xz.y - xz0.y));
        root.r = glm::normalize(undo * root.r);
    }
}

namespace
{
    RootMotion Segment(const AnimationClip& clip, float a, float b)
    {
        glm::vec2 pa, pb, p0;
        float ya, yb, y0;
        clip.SampleRoot(a, pa, ya);
        clip.SampleRoot(b, pb, yb);
        clip.SampleRoot(0.0f, p0, y0);
        RootMotion m;
        // Expressed in the entity's frame: the pose keeps the character facing its first-frame heading.
        const glm::vec2 d = pb - pa;
        m.position = YawRotation(y0 - ya) * glm::vec3(d.x, 0.0f, d.y);
        m.yaw = yb - ya;
        return m;
    }
}

RootMotion ClipRootMotion(const AnimationClip& clip, float a, float b, bool looped)
{
    if (!clip.hasRootMotion) return {};
    if (!looped || b >= a) return Segment(clip, a, b);
    const RootMotion first = Segment(clip, a, clip.duration);
    const RootMotion second = Segment(clip, 0.0f, b);
    RootMotion total;
    total.position = first.position + YawRotation(first.yaw) * second.position;
    total.yaw = first.yaw + second.yaw;
    return total;
}

const AnimationClip* ClipLibrary::Get(const std::string& ref)
{
    if (ref.empty()) return nullptr;
    auto it = m_Clips.find(ref);
    if (it != m_Clips.end()) return it->second.get();
    const size_t hash = ref.rfind('#');
    const std::string path = hash == std::string::npos ? ref : ref.substr(0, hash);
    const std::string take = hash == std::string::npos ? std::string() : ref.substr(hash + 1);
    auto clip = std::make_unique<AnimationClip>();
    if (!LoadFbxClip(path, take, *clip)) clip.reset();
    else if (clip->hasRootMotion) LOG_INFO("Loaded animation %s (%.2f s, root motion %.2f m/s)", ref.c_str(), clip->duration, clip->averageSpeed);
    else LOG_INFO("Loaded animation %s (%.2f s)", ref.c_str(), clip->duration);
    return (m_Clips[ref] = std::move(clip)).get();
}

const ClipBinding& ClipLibrary::Binding(const AnimationClip* clip, const Skeleton* skeleton)
{
    auto& perClip = m_Bindings[clip];
    auto it = perClip.find(skeleton);
    if (it != perClip.end()) return it->second;
    return perClip[skeleton] = BindClip(*clip, *skeleton);
}

void ClipLibrary::Invalidate(const std::string& path)
{
    for (auto it = m_Clips.begin(); it != m_Clips.end();)
    {
        if (it->first == path || it->first.rfind(path + "#", 0) == 0)
        {
            m_Bindings.erase(it->second.get());
            it = m_Clips.erase(it);
        }
        else ++it;
    }
}
