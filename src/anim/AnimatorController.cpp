#include "anim/AnimatorController.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
    const char* kTypeNames[] = { "float", "int", "bool", "trigger" };
    const char* kModeNames[] = { "if", "ifnot", "greater", "less", "equals", "notequal" };
    const char* kMotionNames[] = { "clip", "blend1d", "blend2d" };

    template <typename T, size_t N>
    int IndexOf(const char* (&names)[N], const std::string& s)
    {
        for (size_t i = 0; i < N; ++i)
            if (s == names[i]) return static_cast<int>(i);
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Asset
// ---------------------------------------------------------------------------
bool AnimatorController::IsControllerFile(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".controller") return false;
    std::ifstream in(path);
    std::string header;
    in >> header;
    return header == "TheEngineAnimator";
}

bool AnimatorController::Save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;
    out << "TheEngineAnimator 1\n";
    for (const AnimParam& p : params)
        out << "param " << std::quoted(p.name) << ' ' << kTypeNames[static_cast<int>(p.type)] << ' ' << p.defaultValue << "\n";
    for (const AnimState& s : states)
    {
        out << "state " << std::quoted(s.name) << ' ' << kMotionNames[static_cast<int>(s.type)] << ' ' << std::quoted(s.clip) << ' '
            << std::quoted(s.paramX) << ' ' << std::quoted(s.paramY) << ' ' << s.speed << ' ' << s.loop << ' ' << s.position.x << ' '
            << s.position.y << "\n";
        for (const BlendChild& c : s.children)
            out << "  child " << std::quoted(c.clip) << ' ' << c.threshold << ' ' << c.position.x << ' ' << c.position.y << ' ' << c.speed << "\n";
    }
    for (const AnimTransition& t : transitions)
    {
        out << "transition " << std::quoted(t.from) << ' ' << std::quoted(t.to) << ' ' << t.hasExitTime << ' ' << t.exitTime << ' '
            << t.duration << "\n";
        for (const AnimCondition& c : t.conditions)
            out << "  condition " << std::quoted(c.param) << ' ' << kModeNames[static_cast<int>(c.mode)] << ' ' << c.threshold << "\n";
    }
    out << "default " << std::quoted(defaultState) << "\n";
    out << "entry " << entryPosition.x << ' ' << entryPosition.y << "\n";
    out << "any " << anyStatePosition.x << ' ' << anyStatePosition.y << "\n";
    return static_cast<bool>(out);
}

bool AnimatorController::Load(const std::string& path)
{
    std::ifstream file(path);
    std::string header;
    int version = 0;
    file >> header >> version;
    if (header != "TheEngineAnimator") return false;
    AnimatorController c;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        std::istringstream in(line);
        std::string key;
        if (!(in >> key)) continue;
        if (key == "param")
        {
            AnimParam p;
            std::string type;
            in >> std::quoted(p.name) >> type >> p.defaultValue;
            p.type = static_cast<AnimParamType>(IndexOf<const char*>(kTypeNames, type));
            c.params.push_back(p);
        }
        else if (key == "state")
        {
            AnimState s;
            std::string type;
            in >> std::quoted(s.name) >> type >> std::quoted(s.clip) >> std::quoted(s.paramX) >> std::quoted(s.paramY) >> s.speed >> s.loop >>
                s.position.x >> s.position.y;
            s.type = static_cast<AnimMotionType>(IndexOf<const char*>(kMotionNames, type));
            c.states.push_back(s);
        }
        else if (key == "child" && !c.states.empty())
        {
            BlendChild ch;
            in >> std::quoted(ch.clip) >> ch.threshold >> ch.position.x >> ch.position.y >> ch.speed;
            c.states.back().children.push_back(ch);
        }
        else if (key == "transition")
        {
            AnimTransition t;
            in >> std::quoted(t.from) >> std::quoted(t.to) >> t.hasExitTime >> t.exitTime >> t.duration;
            c.transitions.push_back(t);
        }
        else if (key == "condition" && !c.transitions.empty())
        {
            AnimCondition cond;
            std::string mode;
            in >> std::quoted(cond.param) >> mode >> cond.threshold;
            cond.mode = static_cast<AnimConditionMode>(IndexOf<const char*>(kModeNames, mode));
            c.transitions.back().conditions.push_back(cond);
        }
        else if (key == "default") in >> std::quoted(c.defaultState);
        else if (key == "entry") in >> c.entryPosition.x >> c.entryPosition.y;
        else if (key == "any") in >> c.anyStatePosition.x >> c.anyStatePosition.y;
    }
    *this = std::move(c);
    return true;
}

int AnimatorController::FindState(const std::string& name) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == name) return static_cast<int>(i);
    return -1;
}

int AnimatorController::FindParam(const std::string& name) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == name) return static_cast<int>(i);
    return -1;
}

std::string AnimatorController::UniqueStateName(const std::string& base) const
{
    std::string name = base.empty() ? "New State" : base;
    for (int i = 1; FindState(name) >= 0 || name == kAnyState; ++i) name = base + " " + std::to_string(i);
    return name;
}

void AnimatorController::RenameState(const std::string& from, const std::string& to)
{
    for (AnimState& s : states)
        if (s.name == from) s.name = to;
    for (AnimTransition& t : transitions)
    {
        if (t.from == from) t.from = to;
        if (t.to == from) t.to = to;
    }
    if (defaultState == from) defaultState = to;
}

void AnimatorController::RemoveState(const std::string& name)
{
    states.erase(std::remove_if(states.begin(), states.end(), [&](const AnimState& s) { return s.name == name; }), states.end());
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
                                     [&](const AnimTransition& t) { return t.from == name || t.to == name; }),
                      transitions.end());
    if (defaultState == name) defaultState = states.empty() ? std::string() : states[0].name;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
void AnimatorInstance::Reset(const AnimatorController& controller)
{
    m_Controller = &controller;
    m_Values.clear();
    for (const AnimParam& p : controller.params) m_Values.push_back(p.type == AnimParamType::Trigger ? 0.0f : p.defaultValue);
    m_Current = controller.FindState(controller.defaultState);
    if (m_Current < 0 && !controller.states.empty()) m_Current = 0;
    m_CurrentTime = 0.0f;
    m_Next = -1;
    m_TransitionElapsed = m_TransitionDuration = 0.0f;
}

float AnimatorInstance::GetParam(const std::string& name) const
{
    if (!m_Controller) return 0.0f;
    const int i = m_Controller->FindParam(name);
    return i >= 0 && i < static_cast<int>(m_Values.size()) ? m_Values[i] : 0.0f;
}

bool AnimatorInstance::SetParam(const std::string& name, float value)
{
    if (!m_Controller) return false;
    const int i = m_Controller->FindParam(name);
    if (i < 0 || i >= static_cast<int>(m_Values.size())) return false;
    const AnimParamType type = m_Controller->params[i].type;
    if (type == AnimParamType::Int) value = std::round(value);
    if (type == AnimParamType::Bool || type == AnimParamType::Trigger) value = value != 0.0f ? 1.0f : 0.0f;
    m_Values[i] = value;
    return true;
}

void AnimatorInstance::Weights(const AnimState& state, ClipLibrary& clips, std::vector<WeightedClip>& out) const
{
    out.clear();
    if (state.type == AnimMotionType::Clip)
    {
        if (const AnimationClip* c = clips.Get(state.clip)) out.push_back({ c, 1.0f, 1.0f });
        return;
    }
    std::vector<std::pair<const AnimationClip*, const BlendChild*>> children;
    for (const BlendChild& ch : state.children)
        if (const AnimationClip* c = clips.Get(ch.clip)) children.push_back({ c, &ch });
    if (children.empty()) return;

    if (state.type == AnimMotionType::BlendTree1D)
    {
        const float x = GetParam(state.paramX);
        std::sort(children.begin(), children.end(), [](auto& a, auto& b) { return a.second->threshold < b.second->threshold; });
        if (x <= children.front().second->threshold) { out.push_back({ children.front().first, 1.0f, children.front().second->speed }); return; }
        if (x >= children.back().second->threshold) { out.push_back({ children.back().first, 1.0f, children.back().second->speed }); return; }
        for (size_t i = 0; i + 1 < children.size(); ++i)
        {
            const float a = children[i].second->threshold, b = children[i + 1].second->threshold;
            if (x >= a && x <= b)
            {
                const float t = b > a ? (x - a) / (b - a) : 0.0f;
                out.push_back({ children[i].first, 1.0f - t, children[i].second->speed });
                out.push_back({ children[i + 1].first, t, children[i + 1].second->speed });
                return;
            }
        }
        return;
    }

    // 2D freeform cartesian: gradient band interpolation (Johansen), as in Unity.
    const glm::vec2 p(GetParam(state.paramX), GetParam(state.paramY));
    std::vector<float> w(children.size(), 1.0f);
    float total = 0.0f;
    for (size_t i = 0; i < children.size(); ++i)
    {
        const glm::vec2 pi = children[i].second->position;
        for (size_t j = 0; j < children.size(); ++j)
        {
            if (i == j) continue;
            const glm::vec2 pij = children[j].second->position - pi;
            const float len2 = glm::dot(pij, pij);
            if (len2 < 1e-8f) continue;
            w[i] = std::min(w[i], std::clamp(1.0f - glm::dot(p - pi, pij) / len2, 0.0f, 1.0f));
        }
        total += w[i];
    }
    for (size_t i = 0; i < children.size(); ++i)
        if (w[i] > 1e-4f && total > 0.0f) out.push_back({ children[i].first, w[i] / total, children[i].second->speed });
}

float AnimatorInstance::StateLength(const AnimState& state, ClipLibrary& clips) const
{
    // Blend tree children are time-synchronized: the length is the weighted average of their lengths.
    std::vector<WeightedClip> w;
    Weights(state, clips, w);
    float length = 0.0f;
    for (const WeightedClip& c : w) length += c.weight * c.clip->duration / std::max(c.speed, 1e-3f);
    return std::max(length, 1e-3f);
}

void AnimatorInstance::EvaluateState(const AnimState& state, float normalizedTime, ClipLibrary& clips, const Skeleton& skeleton, bool extract,
                                     Pose& out)
{
    std::vector<WeightedClip> w;
    Weights(state, clips, w);
    if (w.empty())
    {
        out = skeleton.rest;
        return;
    }
    const float n = state.loop ? normalizedTime - std::floor(normalizedTime) : std::clamp(normalizedTime, 0.0f, 1.0f);
    float accumulated = 0.0f;
    Pose sample;
    for (const WeightedClip& c : w)
    {
        Pose& target = accumulated == 0.0f ? out : sample;
        SampleClip(*c.clip, clips.Binding(c.clip, &skeleton), skeleton, n * c.clip->duration, extract, target);
        if (accumulated > 0.0f)
        {
            const float t = c.weight / (accumulated + c.weight);
            for (size_t i = 0; i < out.size(); ++i) out[i] = Blend(out[i], sample[i], t);
        }
        accumulated += c.weight;
    }
}

RootMotion AnimatorInstance::StateMotion(const AnimState& state, float from, float to, ClipLibrary& clips) const
{
    std::vector<WeightedClip> w;
    Weights(state, clips, w);
    RootMotion total;
    if (!state.loop)
    {
        from = std::clamp(from, 0.0f, 1.0f);
        to = std::clamp(to, 0.0f, 1.0f);
    }
    const float wrapsFrom = std::floor(from), wrapsTo = std::floor(to);
    const float a = from - wrapsFrom, b = to - wrapsTo;
    const bool looped = state.loop && wrapsTo > wrapsFrom;
    for (const WeightedClip& c : w)
    {
        const RootMotion m = ClipRootMotion(*c.clip, a * c.clip->duration, (looped || b >= a ? b : a) * c.clip->duration, looped);
        total.position += m.position * c.weight;
        total.yaw += m.yaw * c.weight;
    }
    return total;
}

bool AnimatorInstance::ConditionsMet(const AnimTransition& t) const
{
    for (const AnimCondition& c : t.conditions)
    {
        const float v = GetParam(c.param);
        bool ok = false;
        switch (c.mode)
        {
        case AnimConditionMode::If: ok = v != 0.0f; break;
        case AnimConditionMode::IfNot: ok = v == 0.0f; break;
        case AnimConditionMode::Greater: ok = v > c.threshold; break;
        case AnimConditionMode::Less: ok = v < c.threshold; break;
        case AnimConditionMode::Equals: ok = v == c.threshold; break;
        case AnimConditionMode::NotEqual: ok = v != c.threshold; break;
        }
        if (!ok) return false;
    }
    return true;
}

void AnimatorInstance::ConsumeTriggers(const AnimTransition& t)
{
    for (const AnimCondition& c : t.conditions)
    {
        const int i = m_Controller->FindParam(c.param);
        if (i >= 0 && m_Controller->params[i].type == AnimParamType::Trigger) m_Values[i] = 0.0f;
    }
}

void AnimatorInstance::Start(int state, const AnimTransition* via, ClipLibrary& clips)
{
    (void)clips;
    m_Next = state;
    m_NextTime = 0.0f;
    m_TransitionElapsed = 0.0f;
    m_TransitionDuration = via ? std::max(via->duration, 0.0f) : 0.0f;
}

void AnimatorInstance::Update(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extractRootMotion, Pose& pose, RootMotion& motion)
{
    motion = {};
    if (!m_Controller || m_Current < 0 || m_Current >= static_cast<int>(m_Controller->states.size()))
    {
        pose = skeleton.rest;
        return;
    }
    const auto& states = m_Controller->states;

    // Transitions (not interruptible while one is running, Unity's default).
    if (m_Next < 0)
    {
        const std::string& currentName = states[m_Current].name;
        const float length = StateLength(states[m_Current], clips);
        const float prevTime = m_CurrentTime;
        const float nextTime = m_CurrentTime + dt * states[m_Current].speed / length;
        for (int pass = 0; pass < 2 && m_Next < 0; ++pass)
        {
            for (const AnimTransition& t : m_Controller->transitions)
            {
                const bool fromAny = t.from == AnimatorController::kAnyState;
                if (pass == 0 ? !fromAny : t.from != currentName) continue;
                const int target = m_Controller->FindState(t.to);
                if (target < 0 || (fromAny && target == m_Current)) continue;
                if (t.hasExitTime)
                {
                    // Fires when the normalized time crosses the exit time (every loop for looping states).
                    bool crossed;
                    if (states[m_Current].loop && t.exitTime < 1.0f)
                    {
                        const float a = prevTime - std::floor(prevTime), b = a + (nextTime - prevTime);
                        crossed = (a < t.exitTime && b >= t.exitTime) || (b >= 1.0f + t.exitTime);
                    }
                    else crossed = nextTime >= t.exitTime;
                    if (!crossed) continue;
                }
                if (!t.hasExitTime && t.conditions.empty()) continue; // would fire every frame
                if (!ConditionsMet(t)) continue;
                ConsumeTriggers(t);
                Start(target, &t, clips);
                break;
            }
        }
    }

    // Advance current (and next) state.
    const AnimState& current = states[m_Current];
    const float lengthA = StateLength(current, clips);
    const float fromA = m_CurrentTime;
    m_CurrentTime += dt * current.speed / lengthA;
    EvaluateState(current, m_CurrentTime, clips, skeleton, extractRootMotion, pose);
    RootMotion ma = extractRootMotion ? StateMotion(current, fromA, m_CurrentTime, clips) : RootMotion{};

    if (m_Next >= 0)
    {
        const AnimState& next = states[m_Next];
        const float lengthB = StateLength(next, clips);
        const float fromB = m_NextTime;
        m_NextTime += dt * next.speed / lengthB;
        m_TransitionElapsed += dt;
        const float w = m_TransitionDuration > 0.0f ? std::clamp(m_TransitionElapsed / m_TransitionDuration, 0.0f, 1.0f) : 1.0f;
        EvaluateState(next, m_NextTime, clips, skeleton, extractRootMotion, m_PoseB);
        for (size_t i = 0; i < pose.size() && i < m_PoseB.size(); ++i) pose[i] = Blend(pose[i], m_PoseB[i], w);
        if (extractRootMotion)
        {
            const RootMotion mb = StateMotion(next, fromB, m_NextTime, clips);
            ma.position = ma.position * (1.0f - w) + mb.position * w;
            ma.yaw = ma.yaw * (1.0f - w) + mb.yaw * w;
        }
        if (w >= 1.0f)
        {
            m_Current = m_Next;
            m_CurrentTime = m_NextTime;
            m_Next = -1;
        }
    }
    motion = ma;
}

void AnimatorInstance::SamplePreview(ClipLibrary& clips, const Skeleton& skeleton, Pose& pose)
{
    if (!m_Controller || m_Current < 0 || m_Current >= static_cast<int>(m_Controller->states.size()))
    {
        pose = skeleton.rest;
        return;
    }
    EvaluateState(m_Controller->states[m_Current], 0.0f, clips, skeleton, true, pose);
}
