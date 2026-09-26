#pragma once

#include "anim/Animation.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Animator Controller asset (Unity-style state machine), stored as text in a .controller file.

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

struct AnimatorController
{
    static constexpr const char* kAnyState = "Any State";

    std::vector<AnimParam> params;
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    std::string defaultState;
    glm::vec2 entryPosition{ 40.0f, 200.0f };
    glm::vec2 anyStatePosition{ 40.0f, 320.0f };

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    static bool IsControllerFile(const std::string& path); // .controller written by TheEngine (Unity's YAML ones are skipped)

    int FindState(const std::string& name) const;
    int FindParam(const std::string& name) const;
    std::string UniqueStateName(const std::string& base) const;
    void RenameState(const std::string& from, const std::string& to);
    void RemoveState(const std::string& name);
};

// Runtime state of one Animator (parameters, current state, transition in progress).
class AnimatorInstance
{
public:
    void Reset(const AnimatorController& controller);
    bool Valid() const { return m_Controller != nullptr; }

    float GetParam(const std::string& name) const;
    bool SetParam(const std::string& name, float value); // triggers: value != 0 sets, 0 resets
    const std::vector<float>& Values() const { return m_Values; }

    // Advances the state machine and produces the pose (and root motion when extracting it).
    void Update(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extractRootMotion, Pose& pose, RootMotion& motion);
    // Pose of the default state's first frame (edit mode preview).
    void SamplePreview(ClipLibrary& clips, const Skeleton& skeleton, Pose& pose);

    int CurrentState() const { return m_Current; }
    int NextState() const { return m_Next; }
    float CurrentNormalizedTime() const { return m_CurrentTime; }
    float TransitionProgress() const { return m_Next >= 0 && m_TransitionDuration > 0 ? m_TransitionElapsed / m_TransitionDuration : 0.0f; }
    const AnimatorController* Controller() const { return m_Controller; }

private:
    struct WeightedClip
    {
        const AnimationClip* clip;
        float weight;
        float speed;
    };
    void Weights(const AnimState& state, ClipLibrary& clips, std::vector<WeightedClip>& out) const;
    float StateLength(const AnimState& state, ClipLibrary& clips) const; // seconds for normalized time 0..1
    void EvaluateState(const AnimState& state, float normalizedTime, ClipLibrary& clips, const Skeleton& skeleton, bool extract, Pose& out);
    RootMotion StateMotion(const AnimState& state, float fromNormalized, float toNormalized, ClipLibrary& clips) const;
    bool ConditionsMet(const AnimTransition& t) const;
    void ConsumeTriggers(const AnimTransition& t);
    void Start(int state, const AnimTransition* via, ClipLibrary& clips);

    const AnimatorController* m_Controller = nullptr;
    std::vector<float> m_Values;
    int m_Current = -1;
    float m_CurrentTime = 0.0f; // normalized, keeps counting over loops
    int m_Next = -1;
    float m_NextTime = 0.0f;
    float m_TransitionElapsed = 0.0f;
    float m_TransitionDuration = 0.0f;
    Pose m_PoseA, m_PoseB;
};
