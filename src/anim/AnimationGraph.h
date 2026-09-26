#pragma once

#include "anim/AnimationStream.h"
#include "anim/AnimatorController.h"

#include <functional>
#include <string>
#include <vector>

// A small pull-evaluated playable graph. Sources produce poses; mixers combine them; jobs get a
// writable stream. A graph is owned by its caller, while AnimatorInstance remains the owner of
// controller state and parameters. Node IDs are stable until Clear().
class AnimationGraph
{
public:
    using NodeId = int;
    using Job = std::function<void(AnimationStream&)>;

    NodeId AddController(AnimatorInstance& instance);
    NodeId AddClip(std::string clip, float speed = 1.0f, bool loop = true);
    NodeId AddMixer(NodeId a, NodeId b, float weight);
    NodeId AddJob(NodeId input, Job job);
    bool SetMixerWeight(NodeId node, float weight);
    bool SetClipSpeed(NodeId node, float speed);
    bool SeekClip(NodeId node, float seconds);
    float ClipTime(NodeId node) const;
    void SetOutput(NodeId node) { m_Output = node; }
    void Clear() { m_Nodes.clear(); m_Output = -1; }
    bool Evaluate(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool playing,
                  bool extractRootMotion, Pose& pose, RootMotion& motion);

private:
    struct Node
    {
        enum class Type { Controller, Clip, Mixer, Job } type = Type::Clip;
        int a = -1, b = -1;
        AnimatorInstance* controller = nullptr;
        std::string clip;
        float time = 0.0f, speed = 1.0f, weight = 0.5f;
        bool loop = true;
        Job job;
        Pose pose;
        RootMotion motion;
        uint8_t visit = 0;
    };
    bool EvaluateNode(NodeId id, float dt, ClipLibrary& clips, const Skeleton& skeleton,
                      bool playing, bool extractRootMotion);
    std::vector<Node> m_Nodes;
    NodeId m_Output = -1;
};
