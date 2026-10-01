#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Skeletal animation data. All transforms are in "model space": the imported file converted to Y-up meters with
// the character facing -Z (the engine's forward). The top-level bone's local transform is relative to model space.

struct BoneTransform
{
    glm::vec3 t{ 0.0f };
    glm::quat r{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 s{ 1.0f };
};

BoneTransform Blend(const BoneTransform& a, const BoneTransform& b, float t);
glm::mat4 ToMatrix(const BoneTransform& x);

struct Skeleton
{
    std::vector<std::string> names;
    std::vector<int> parents;         // parents precede children (-1 = top level)
    std::vector<BoneTransform> rest;  // local bind/rest pose
    std::unordered_map<std::string, int> index;
    int root = -1;   // bone carrying root motion ("root", else the first top-level bone)
    int pelvis = -1; // hips ("pelvis" / "hips"), its translation is animated

    int Find(const std::string& name) const
    {
        auto it = index.find(name);
        return it != index.end() ? it->second : -1;
    }
};

using Pose = std::vector<BoneTransform>; // local transform per skeleton bone

// Local -> model space matrices.
void PoseToModel(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& model);

// Planar root motion (XZ translation + yaw), like Unity's "Root Transform Position (XZ)" / "Rotation".
struct RootMotion
{
    glm::vec3 position{ 0.0f }; // in the character's current local frame
    float yaw = 0.0f;           // radians, positive = counter-clockwise seen from above
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    float fps = 30.0f;
    int frameCount = 0;
    // Tracks by bone name, frameCount samples each (local transforms).
    struct Track
    {
        std::string bone;
        std::vector<BoneTransform> frames;
        BoneTransform rest;
    };
    std::vector<Track> tracks;
    std::unordered_map<std::string, int> trackIndex;

    // Root bone motion per frame (model space): planar position and heading.
    int rootTrack = -1;
    std::vector<glm::vec2> rootXZ;
    std::vector<float> rootYaw;
    bool hasRootMotion = false; // the root bone travels or turns
    float averageSpeed = 0.0f;  // m/s over the clip (for the editor)

    // Float curves keyed by name, each a list of (seconds, value), read from the Unity .anim beside the clip file
    // (e.g. properties authored alongside imported animation clips).
    std::unordered_map<std::string, std::vector<glm::vec2>> floatCurves;
    bool SampleFloat(const std::string& name, float seconds, float& value) const;

    BoneTransform SampleTrack(int track, float time) const;
    // Planar root position/yaw at a time (interpolated).
    void SampleRoot(float time, glm::vec2& xz, float& yaw) const;
};

// Remaps a clip's tracks onto a skeleton by bone name (cached per clip/skeleton pair by the user).
struct ClipBinding
{
    std::vector<int> trackForBone; // -1 = keep rest pose
    std::vector<uint8_t> animatedTranslation; // IK / marker bones whose clip translation is used as authored
    float pelvisScale = 1.0f;       // target / source hip height
};
ClipBinding BindClip(const AnimationClip& clip, const Skeleton& skeleton);

// Samples a clip into a pose at time (seconds). With extractRootMotion, the root bone's planar motion relative to
// the first frame is removed from the pose (the entity moves instead).
// sampleScale opts additive sampling into authored scale relative to the source track's rest scale.
void SampleClip(const AnimationClip& clip, const ClipBinding& binding, const Skeleton& skeleton, float time, bool extractRootMotion,
                Pose& out, bool sampleScale = false);

// Root motion from time a to b (handles looping wrap when b < a).
RootMotion ClipRootMotion(const AnimationClip& clip, float a, float b, bool looped);

// Imported animation clips (FBX), cached by reference "path" or "path#TakeName".
class ClipLibrary
{
public:
    const AnimationClip* Get(const std::string& ref);
    const ClipBinding& Binding(const AnimationClip* clip, const Skeleton* skeleton);
    void ForgetSkeleton(const Skeleton* skeleton);
    std::vector<std::string> TakeNames(const std::string& path); // animation stacks in a file
    void Invalidate(const std::string& path);
    void Add(const std::string& ref, const AnimationClip& clip) { m_Clips[ref] = std::make_unique<AnimationClip>(clip); } // tests / generated clips
    void Clear() { m_Clips.clear(); m_Bindings.clear(); }

private:
    std::unordered_map<std::string, std::unique_ptr<AnimationClip>> m_Clips; // null = failed
    std::unordered_map<const void*, std::unordered_map<const void*, ClipBinding>> m_Bindings;
};

bool LoadFbxClip(const std::string& path, const std::string& take, AnimationClip& out);
