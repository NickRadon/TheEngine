#include "anim/AnimationGraph.h"

#include <algorithm>
#include <cmath>
#include <utility>

AnimationGraph::NodeId AnimationGraph::AddController(AnimatorInstance& instance)
{
    Node node;
    node.type = Node::Type::Controller;
    node.controller = &instance;
    m_Nodes.push_back(std::move(node));
    return static_cast<NodeId>(m_Nodes.size() - 1);
}

AnimationGraph::NodeId AnimationGraph::AddClip(std::string clip, float speed, bool loop)
{
    Node node;
    node.type = Node::Type::Clip;
    node.clip = std::move(clip);
    node.speed = speed;
    node.loop = loop;
    m_Nodes.push_back(std::move(node));
    return static_cast<NodeId>(m_Nodes.size() - 1);
}

AnimationGraph::NodeId AnimationGraph::AddMixer(NodeId a, NodeId b, float weight)
{
    Node node;
    node.type = Node::Type::Mixer;
    node.a = a;
    node.b = b;
    node.weight = weight;
    m_Nodes.push_back(std::move(node));
    return static_cast<NodeId>(m_Nodes.size() - 1);
}

AnimationGraph::NodeId AnimationGraph::AddJob(NodeId input, Job job)
{
    Node node;
    node.type = Node::Type::Job;
    node.a = input;
    node.job = std::move(job);
    m_Nodes.push_back(std::move(node));
    return static_cast<NodeId>(m_Nodes.size() - 1);
}

bool AnimationGraph::SetMixerWeight(NodeId id, float weight)
{
    if (id < 0 || id >= static_cast<NodeId>(m_Nodes.size()) || m_Nodes[id].type != Node::Type::Mixer) return false;
    m_Nodes[id].weight = std::clamp(weight, 0.0f, 1.0f);
    return true;
}

bool AnimationGraph::SetClipSpeed(NodeId id, float speed)
{
    if (id < 0 || id >= static_cast<NodeId>(m_Nodes.size()) || m_Nodes[id].type != Node::Type::Clip) return false;
    m_Nodes[id].speed = speed;
    return true;
}

bool AnimationGraph::SeekClip(NodeId id, float seconds)
{
    if (id < 0 || id >= static_cast<NodeId>(m_Nodes.size()) || m_Nodes[id].type != Node::Type::Clip) return false;
    m_Nodes[id].time = std::max(0.0f, seconds);
    return true;
}

float AnimationGraph::ClipTime(NodeId id) const
{
    return id >= 0 && id < static_cast<NodeId>(m_Nodes.size()) && m_Nodes[id].type == Node::Type::Clip ?
           m_Nodes[id].time : 0.0f;
}

bool AnimationGraph::EvaluateNode(NodeId id, float dt, ClipLibrary& clips, const Skeleton& skeleton,
                                  bool playing, bool extractRootMotion)
{
    if (id < 0 || id >= static_cast<NodeId>(m_Nodes.size())) return false;
    Node& node = m_Nodes[id];
    if (node.visit == 2) return true;
    if (node.visit == 1) return false; // cycle
    node.visit = 1;
    switch (node.type)
    {
    case Node::Type::Controller:
        if (!node.controller || !node.controller->Valid()) return false;
        node.motion = {};
        if (playing) node.controller->Update(dt, clips, skeleton, extractRootMotion, node.pose, node.motion);
        else node.controller->SamplePreview(clips, skeleton, node.pose);
        break;
    case Node::Type::Clip:
    {
        const AnimationClip* clip = clips.Get(node.clip);
        if (!clip) return false;
        const float previous = node.time;
        if (playing)
        {
            node.time += dt * node.speed;
            if (node.loop && clip->duration > 0.0f)
                node.time = std::fmod(std::fmod(node.time, clip->duration) + clip->duration, clip->duration);
            else node.time = std::clamp(node.time, 0.0f, clip->duration);
        }
        SampleClip(*clip, clips.Binding(clip, &skeleton), skeleton, node.time, extractRootMotion, node.pose);
        node.motion = playing && extractRootMotion ? ClipRootMotion(*clip, previous, node.time, node.loop) : RootMotion{};
        break;
    }
    case Node::Type::Mixer:
    {
        if (!EvaluateNode(node.a, dt, clips, skeleton, playing, extractRootMotion) ||
            !EvaluateNode(node.b, dt, clips, skeleton, playing, extractRootMotion)) return false;
        const float w = std::clamp(node.weight, 0.0f, 1.0f);
        const Node& a = m_Nodes[node.a];
        const Node& b = m_Nodes[node.b];
        if (a.pose.size() != b.pose.size()) return false;
        node.pose.resize(a.pose.size());
        for (size_t i = 0; i < node.pose.size(); ++i) node.pose[i] = Blend(a.pose[i], b.pose[i], w);
        node.motion.position = glm::mix(a.motion.position, b.motion.position, w);
        node.motion.yaw = glm::mix(a.motion.yaw, b.motion.yaw, w);
        break;
    }
    case Node::Type::Job:
        if (!EvaluateNode(node.a, dt, clips, skeleton, playing, extractRootMotion)) return false;
        node.pose = m_Nodes[node.a].pose;
        node.motion = m_Nodes[node.a].motion;
        if (node.job)
        {
            AnimationStream stream(skeleton, node.pose, node.motion);
            node.job(stream);
        }
        break;
    }
    node.visit = 2;
    return true;
}

bool AnimationGraph::Evaluate(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool playing,
                              bool extractRootMotion, Pose& pose, RootMotion& motion)
{
    for (Node& node : m_Nodes) node.visit = 0;
    if (!EvaluateNode(m_Output, dt, clips, skeleton, playing, extractRootMotion)) return false;
    pose = m_Nodes[m_Output].pose;
    motion = m_Nodes[m_Output].motion;
    return true;
}
