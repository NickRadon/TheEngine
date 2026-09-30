#pragma once

#include "anim/Animation.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

// Animator Controller asset (Unity-style layered state machines), stored as text in a .controller file.

enum class AnimParamType : int { Float = 0, Int, Bool, Trigger };

struct AnimParam
{
    std::string name;
    AnimParamType type = AnimParamType::Float;
    float defaultValue = 0.0f;
};

enum class AnimMotionType : int { Clip = 0, BlendTree1D, BlendTree2D };

struct BlendChild
{
    std::string clip;
    float threshold = 0.0f;       // 1D
    glm::vec2 position{ 0.0f };   // 2D (freeform cartesian)
    float speed = 1.0f;
};

// Model-space adjustment applied to a sampled state pose before layers are combined.
struct AnimPoseOffset
{
    std::string bone;
    glm::vec3 position{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
};

struct AnimState
{
    std::string name;
    AnimMotionType type = AnimMotionType::Clip;
    std::string clip;              // Clip
    std::string paramX, paramY;    // blend tree parameters
    std::vector<BlendChild> children;
    std::vector<AnimPoseOffset> offsets;
    float speed = 1.0f;
    bool loop = true;
    glm::vec2 position{ 0.0f };    // node position in the Animator window
};

enum class AnimConditionMode : int { If = 0, IfNot, Greater, Less, Equals, NotEqual };

struct AnimCondition
{
    std::string param;
    AnimConditionMode mode = AnimConditionMode::Greater;
    float threshold = 0.0f;
};

// Which transitions may fire while this transition itself is running (Unity's "Interruption Source").
enum class AnimInterruption : int
{
    None = 0,             // the running transition always finishes (TheEngine's pre-3.0 behavior)
    Current,              // transitions leaving the source state
    Next,                 // transitions leaving the destination state
    CurrentThenNext,      // source first, then destination (list order inside each group)
    NextThenCurrent,      // destination first, then source
};

struct AnimTransition
{
    std::string from;              // kAnyState for Any State transitions
    std::string to;
    bool hasExitTime = false;
    float exitTime = 0.75f;        // normalized
    float duration = 0.25f;        // seconds
    std::vector<AnimCondition> conditions;
    // Interruption of this transition while it plays (ignored once it has finished).
    AnimInterruption interruption = AnimInterruption::None;
    // Unity's "Ordered Interruption": candidates are checked in the order they are listed in the
    // layer instead of Any State transitions first.
    bool ordered = false;
};

enum class AnimLayerBlending : int { Override = 0, Additive };

// One state machine. Layers above the base one are blended over it, limited to their avatar mask.
struct AnimLayer
{
    static constexpr const char* kAnyState = "Any State";

    std::string name = "Base Layer";
    float weight = 1.0f;
    AnimLayerBlending blending = AnimLayerBlending::Override;
    std::string referenceClip; // optional neutral clip for additive deltas; empty = state's first frame
    std::vector<std::string> mask;  // inline bones whose subtrees this layer affects (empty = whole body)
    bool maskExact = false;         // when true, inline mask entries do not include descendants
    // Blend mask asset (.mask) shared with other controllers. When assigned it replaces `mask`.
    std::string maskAsset;
    std::vector<std::string> maskAssetBones;          // resolved from maskAsset (not serialized)
    bool maskAssetMissing = false;                    // resolved: the file could not be read
    std::filesystem::file_time_type maskAssetStamp{}; // resolved: file time of maskAssetBones
    // Bones this layer drives: the mask asset's list when one is assigned, otherwise its own list.
    const std::vector<std::string>& EffectiveMask() const { return maskAsset.empty() ? mask : maskAssetBones; }
    // Loads (or reloads) the .mask file behind maskAsset; returns true when the resolved bones changed.
    bool RefreshMaskAsset();
    // False when an assigned mask asset restricts the layer (even to an empty or unreadable list).
    bool DrivesWholeBody() const;
    // Override layers: the mask's top bones take the layer's rotation in model space instead of relative to their
    // parent (Unreal's "mesh space rotation blend"), so an upper body keeps facing where its clip intends even
    // when the base layer turns the hips differently.
    bool meshSpaceRotation = false;
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    std::string defaultState;
    glm::vec2 entryPosition{ 40.0f, 200.0f };
    glm::vec2 anyStatePosition{ 40.0f, 320.0f };

    int FindState(const std::string& n) const;
    std::string UniqueStateName(const std::string& base) const;
    void RenameState(const std::string& from, const std::string& to);
    void RemoveState(const std::string& n);
};

// One problem the editor or the runtime found in a controller; `layer`, `state` and `transition` point
// at the offending element of the layer (index, -1 = not applicable) so the Animator window can select it.
struct AnimIssue
{
    int layer = -1;
    int state = -1;
    int transition = -1;
    std::string text;
};

struct AnimatorController
{
    static constexpr const char* kAnyState = AnimLayer::kAnyState;
    // 1 = single layer, 2 = layers, 3 = interruption, 4 = blend mask, 5 = additive reference and pose offsets
    static constexpr int kCurrentVersion = 5;

    std::vector<AnimParam> params;
    std::vector<AnimLayer> layers{ AnimLayer{} }; // layer 0 is the base layer

    bool Load(const std::string& path);
    bool Load(std::istream& in);
    bool LoadString(const std::string& text);
    bool Save(const std::string& path) const;
    bool Save(std::ostream& out) const;
    std::string ToString() const; // serialized form (undo snapshots, editor state)
    static bool IsControllerFile(const std::string& path); // .controller written by TheEngine (Unity's YAML ones are skipped)

    int FindParam(const std::string& name) const;
    // Problems worth showing in the editor: missing clips/parameters, broken transitions, unreachable
    // states, empty blend trees, mask bones that are not in the rig. `clips` / `skeleton` may be null.
    std::vector<AnimIssue> Validate(class ClipLibrary* clips, const Skeleton* skeleton) const;
    AnimLayer& Base() { return layers[0]; }
    const AnimLayer& Base() const { return layers[0]; }
};

// Runtime state of one Animator (parameters, current state per layer, transitions in progress).
class AnimatorInstance
{
public:
    void Reset(const AnimatorController& controller);
    bool Valid() const { return m_Controller != nullptr; }

    float GetParam(const std::string& name) const;
    bool SetParam(const std::string& name, float value); // triggers: value != 0 sets, 0 resets
    // Exponential approach towards a target (Unity's damped SetFloat): dampTime <= 0 or dt <= 0 set directly.
    bool SetParamDamped(const std::string& name, float target, float dampTime, float dt);
    bool HasParam(const std::string& name) const { return m_Controller && m_Controller->FindParam(name) >= 0; }
    const std::vector<float>& Values() const { return m_Values; }
    float LayerWeight(int layer) const { return layer >= 0 && layer < static_cast<int>(m_Layers.size()) ? m_Layers[layer].weight : 0.0f; }
    void SetLayerWeight(int layer, float weight);
    int LayerCount() const { return static_cast<int>(m_Layers.size()); }

    // Advances every layer and produces the combined pose (and the base layer's root motion when extracting).
    void Update(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extractRootMotion, Pose& pose, RootMotion& motion);
    // Pose of each layer's default state at its first frame (edit mode preview).
    void SamplePreview(ClipLibrary& clips, const Skeleton& skeleton, Pose& pose);

    int CurrentState(int layer = 0) const { return Layer(layer).current; }
    int NextState(int layer = 0) const { return Layer(layer).next; }
    float CurrentNormalizedTime(int layer = 0) const { return Layer(layer).time; }
    bool IsInTransition(int layer = 0) const { return Layer(layer).next >= 0; }
    bool TransitionInterrupted(int layer = 0) const { return Layer(layer).frozen; }
    float TransitionProgress(int layer = 0) const
    {
        const LayerState& l = Layer(layer);
        return l.next >= 0 && l.duration > 0 ? l.elapsed / l.duration : 0.0f;
    }
    const AnimatorController* Controller() const { return m_Controller; }

private:
    struct MotionSource
    {
        int state = -1;
        float time = 0.0f;  // normalized, advances while the blend is frozen
        float base = 1.0f;  // weight inside the frozen blend (sources add up to 1)
    };
    struct LayerState
    {
        int current = -1;
        float time = 0.0f;      // normalized, keeps counting over loops
        int next = -1;
        float nextTime = 0.0f;
        float elapsed = 0.0f;
        float duration = 0.0f;
        int transition = -1;    // index of the running transition in the layer (-1 = none)
        float weight = 1.0f;
        // Set after the running transition was interrupted: the interrupted blend stops progressing but
        // keeps playing (states, times, weights) as the "from" side of the new transition, so there is no
        // pose pop and the interrupted states keep contributing root motion.
        bool frozen = false;
        std::vector<MotionSource> sources;
        std::vector<uint8_t> mask; // per bone of the skeleton it was built for
        const Skeleton* maskSkeleton = nullptr;
    };
    struct WeightedClip
    {
        const AnimationClip* clip;
        float weight;
        float speed;
    };
    const LayerState& Layer(int i) const
    {
        static const LayerState none;
        return i >= 0 && i < static_cast<int>(m_Layers.size()) ? m_Layers[i] : none;
    }
    void Weights(const AnimState& state, ClipLibrary& clips, std::vector<WeightedClip>& out) const;
    float StateLength(const AnimState& state, ClipLibrary& clips) const; // seconds for normalized time 0..1
    void EvaluateState(const AnimState& state, float normalizedTime, ClipLibrary& clips, const Skeleton& skeleton, bool extract, Pose& out);
    RootMotion StateMotion(const AnimState& state, float fromNormalized, float toNormalized, ClipLibrary& clips) const;
    bool ConditionsMet(const AnimTransition& t) const;
    void ConsumeTriggers(const AnimTransition& t);
    // Index of the transition that starts this frame (-1 = none). While `ls` has a transition running only
    // candidates its interruption source allows are considered; exit times are checked against the state
    // each candidate leaves.
    int FindTransition(const AnimLayer& layer, LayerState& ls, float curAfter, float nextAfter);
    static bool ExitCrossed(const AnimState& state, float prev, float after, float exitTime);
    void UpdateLayer(int index, float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extract, Pose& pose, RootMotion& motion);
    const std::vector<uint8_t>& Mask(int index, const Skeleton& skeleton);

    const AnimatorController* m_Controller = nullptr;
    std::vector<float> m_Values;
    std::vector<int> m_ConsumedTriggers; // cleared after every layer has evaluated this frame
    std::vector<LayerState> m_Layers;
    Pose m_PoseB, m_PoseC, m_LayerPose, m_RefPose;
    std::vector<glm::mat4> m_BaseModel, m_LayerModel;
};
