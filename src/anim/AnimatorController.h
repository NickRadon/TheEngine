#pragma once

#include "anim/Animation.h"

#include <glm/glm.hpp>

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

struct AnimState
{
    std::string name;
    AnimMotionType type = AnimMotionType::Clip;
    std::string clip;              // Clip
    std::string paramX, paramY;    // blend tree parameters
    std::vector<BlendChild> children;
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

struct AnimTransition
{
    std::string from;              // kAnyState for Any State transitions
    std::string to;
    bool hasExitTime = false;
    float exitTime = 0.75f;        // normalized
    float duration = 0.25f;        // seconds
    std::vector<AnimCondition> conditions;
};

enum class AnimLayerBlending : int { Override = 0, Additive };

// One state machine. Layers above the base one are blended over it, limited to their avatar mask.
struct AnimLayer
{
    static constexpr const char* kAnyState = "Any State";

    std::string name = "Base Layer";
    float weight = 1.0f;
    AnimLayerBlending blending = AnimLayerBlending::Override;
    std::vector<std::string> mask;  // bones whose subtrees this layer affects (empty = whole body)
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

struct AnimatorController
{
    static constexpr const char* kAnyState = AnimLayer::kAnyState;

    std::vector<AnimParam> params;
    std::vector<AnimLayer> layers{ AnimLayer{} }; // layer 0 is the base layer

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    static bool IsControllerFile(const std::string& path); // .controller written by TheEngine (Unity's YAML ones are skipped)

    int FindParam(const std::string& name) const;
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
    const std::vector<float>& Values() const { return m_Values; }
    float LayerWeight(int layer) const { return layer >= 0 && layer < static_cast<int>(m_Layers.size()) ? m_Layers[layer].weight : 0.0f; }
    void SetLayerWeight(int layer, float weight);

    // Advances every layer and produces the combined pose (and the base layer's root motion when extracting).
    void Update(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extractRootMotion, Pose& pose, RootMotion& motion);
    // Pose of each layer's default state at its first frame (edit mode preview).
    void SamplePreview(ClipLibrary& clips, const Skeleton& skeleton, Pose& pose);

    int CurrentState(int layer = 0) const { return Layer(layer).current; }
    int NextState(int layer = 0) const { return Layer(layer).next; }
    float CurrentNormalizedTime(int layer = 0) const { return Layer(layer).time; }
    float TransitionProgress(int layer = 0) const
    {
        const LayerState& l = Layer(layer);
        return l.next >= 0 && l.duration > 0 ? l.elapsed / l.duration : 0.0f;
    }
    const AnimatorController* Controller() const { return m_Controller; }

private:
    struct LayerState
    {
        int current = -1;
        float time = 0.0f;      // normalized, keeps counting over loops
        int next = -1;
        float nextTime = 0.0f;
        float elapsed = 0.0f;
        float duration = 0.0f;
        float weight = 1.0f;
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
    void UpdateLayer(int index, float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extract, Pose& pose, RootMotion& motion);
    const std::vector<uint8_t>& Mask(int index, const Skeleton& skeleton);

    const AnimatorController* m_Controller = nullptr;
    std::vector<float> m_Values;
    std::vector<LayerState> m_Layers;
    Pose m_PoseB, m_LayerPose, m_RefPose;
    std::vector<glm::mat4> m_BaseModel, m_LayerModel;
};
