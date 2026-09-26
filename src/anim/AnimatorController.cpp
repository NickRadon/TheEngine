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
    const char* kBlendNames[] = { "override", "additive" };

    template <size_t N>
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
    out << "TheEngineAnimator 2\n";
    for (const AnimParam& p : params)
        out << "param " << std::quoted(p.name) << ' ' << kTypeNames[static_cast<int>(p.type)] << ' ' << p.defaultValue << "\n";
    for (const AnimLayer& l : layers)
    {
        out << "layer " << std::quoted(l.name) << ' ' << l.weight << ' ' << kBlendNames[static_cast<int>(l.blending)];
        for (const std::string& bone : l.mask) out << ' ' << std::quoted(bone);
        if (l.meshSpaceRotation) out << " @meshspace";
        out << "\n";
        for (const AnimState& s : l.states)
        {
            out << "state " << std::quoted(s.name) << ' ' << kMotionNames[static_cast<int>(s.type)] << ' ' << std::quoted(s.clip) << ' '
                << std::quoted(s.paramX) << ' ' << std::quoted(s.paramY) << ' ' << s.speed << ' ' << s.loop << ' ' << s.position.x << ' '
                << s.position.y << "\n";
            for (const BlendChild& c : s.children)
                out << "  child " << std::quoted(c.clip) << ' ' << c.threshold << ' ' << c.position.x << ' ' << c.position.y << ' ' << c.speed << "\n";
        }
        for (const AnimTransition& t : l.transitions)
        {
            out << "transition " << std::quoted(t.from) << ' ' << std::quoted(t.to) << ' ' << t.hasExitTime << ' ' << t.exitTime << ' '
                << t.duration << "\n";
            for (const AnimCondition& c : t.conditions)
                out << "  condition " << std::quoted(c.param) << ' ' << kModeNames[static_cast<int>(c.mode)] << ' ' << c.threshold << "\n";
        }
        out << "default " << std::quoted(l.defaultState) << "\n";
        out << "entry " << l.entryPosition.x << ' ' << l.entryPosition.y << "\n";
        out << "any " << l.anyStatePosition.x << ' ' << l.anyStatePosition.y << "\n";
    }
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
    c.layers.clear();
    // Version 1 files have no layer lines: everything belongs to the base layer.
    auto layer = [&]() -> AnimLayer& {
        if (c.layers.empty()) c.layers.emplace_back();
        return c.layers.back();
    };
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
            p.type = static_cast<AnimParamType>(IndexOf(kTypeNames, type));
            c.params.push_back(p);
        }
        else if (key == "layer")
        {
            AnimLayer l;
            std::string blending;
            in >> std::quoted(l.name) >> l.weight >> blending;
            l.blending = static_cast<AnimLayerBlending>(IndexOf(kBlendNames, blending));
            std::string bone;
            while (in >> std::quoted(bone))
            {
                if (bone == "@meshspace") l.meshSpaceRotation = true;
                else l.mask.push_back(bone);
            }
            c.layers.push_back(l);
        }
        else if (key == "state")
        {
            AnimState s;
            std::string type;
            in >> std::quoted(s.name) >> type >> std::quoted(s.clip) >> std::quoted(s.paramX) >> std::quoted(s.paramY) >> s.speed >> s.loop >>
                s.position.x >> s.position.y;
            s.type = static_cast<AnimMotionType>(IndexOf(kMotionNames, type));
            layer().states.push_back(s);
        }
        else if (key == "child" && !layer().states.empty())
        {
            BlendChild ch;
            in >> std::quoted(ch.clip) >> ch.threshold >> ch.position.x >> ch.position.y >> ch.speed;
            layer().states.back().children.push_back(ch);
        }
        else if (key == "transition")
        {
            AnimTransition t;
            in >> std::quoted(t.from) >> std::quoted(t.to) >> t.hasExitTime >> t.exitTime >> t.duration;
            layer().transitions.push_back(t);
        }
        else if (key == "condition" && !layer().transitions.empty())
        {
            AnimCondition cond;
            std::string mode;
            in >> std::quoted(cond.param) >> mode >> cond.threshold;
            cond.mode = static_cast<AnimConditionMode>(IndexOf(kModeNames, mode));
            layer().transitions.back().conditions.push_back(cond);
        }
        else if (key == "default") in >> std::quoted(layer().defaultState);
        else if (key == "entry") in >> layer().entryPosition.x >> layer().entryPosition.y;
        else if (key == "any") in >> layer().anyStatePosition.x >> layer().anyStatePosition.y;
    }
    if (c.layers.empty()) c.layers.emplace_back();
    *this = std::move(c);
    return true;
}

int AnimatorController::FindParam(const std::string& name) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == name) return static_cast<int>(i);
    return -1;
}

int AnimLayer::FindState(const std::string& n) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == n) return static_cast<int>(i);
    return -1;
}

std::string AnimLayer::UniqueStateName(const std::string& base) const
{
    std::string n = base.empty() ? "New State" : base;
    for (int i = 1; FindState(n) >= 0 || n == kAnyState; ++i) n = base + " " + std::to_string(i);
    return n;
}

void AnimLayer::RenameState(const std::string& from, const std::string& to)
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

void AnimLayer::RemoveState(const std::string& n)
{
    states.erase(std::remove_if(states.begin(), states.end(), [&](const AnimState& s) { return s.name == n; }), states.end());
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(), [&](const AnimTransition& t) { return t.from == n || t.to == n; }),
                      transitions.end());
    if (defaultState == n) defaultState = states.empty() ? std::string() : states[0].name;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
void AnimatorInstance::Reset(const AnimatorController& controller)
{
    m_Controller = &controller;
    m_Values.clear();
    for (const AnimParam& p : controller.params) m_Values.push_back(p.type == AnimParamType::Trigger ? 0.0f : p.defaultValue);
    m_Layers.assign(controller.layers.size(), LayerState{});
    for (size_t i = 0; i < controller.layers.size(); ++i)
    {
        const AnimLayer& l = controller.layers[i];
        LayerState& s = m_Layers[i];
        s.current = l.FindState(l.defaultState);
        if (s.current < 0 && !l.states.empty()) s.current = 0;
        s.weight = i == 0 ? 1.0f : l.weight;
    }
}

void AnimatorInstance::SetLayerWeight(int layer, float weight)
{
    if (layer > 0 && layer < static_cast<int>(m_Layers.size())) m_Layers[layer].weight = std::clamp(weight, 0.0f, 1.0f);
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

const std::vector<uint8_t>& AnimatorInstance::Mask(int index, const Skeleton& skeleton)
{
    LayerState& l = m_Layers[index];
    if (l.maskSkeleton == &skeleton && l.mask.size() == skeleton.names.size()) return l.mask;
    const AnimLayer& layer = m_Controller->layers[index];
    l.mask.assign(skeleton.names.size(), layer.mask.empty() ? 1 : 0);
    // A bone is in the mask when it or one of its ancestors is listed (parents precede children).
    for (const std::string& bone : layer.mask)
        if (const int b = skeleton.Find(bone); b >= 0) l.mask[b] = 1;
    for (size_t i = 0; i < skeleton.names.size(); ++i)
        if (skeleton.parents[i] >= 0 && l.mask[skeleton.parents[i]]) l.mask[i] = 1;
    l.maskSkeleton = &skeleton;
    return l.mask;
}

void AnimatorInstance::UpdateLayer(int index, float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extract, Pose& pose, RootMotion& motion)
{
    motion = {};
    const AnimLayer& layer = m_Controller->layers[index];
    LayerState& ls = m_Layers[index];
    const auto& states = layer.states;
    if (ls.current < 0 || ls.current >= static_cast<int>(states.size()))
    {
        pose = skeleton.rest;
        return;
    }

    // Transitions (not interruptible while one is running, Unity's default).
    if (ls.next < 0)
    {
        const std::string& currentName = states[ls.current].name;
        const float length = StateLength(states[ls.current], clips);
        const float prevTime = ls.time;
        const float nextTime = ls.time + dt * states[ls.current].speed / length;
        for (int pass = 0; pass < 2 && ls.next < 0; ++pass)
        {
            for (const AnimTransition& t : layer.transitions)
            {
                const bool fromAny = t.from == AnimLayer::kAnyState;
                if (pass == 0 ? !fromAny : t.from != currentName) continue;
                const int target = layer.FindState(t.to);
                if (target < 0 || (fromAny && target == ls.current)) continue;
                if (t.hasExitTime)
                {
                    // Fires when the normalized time crosses the exit time (every loop for looping states).
                    bool crossed;
                    if (states[ls.current].loop && t.exitTime < 1.0f)
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
                ls.next = target;
                ls.nextTime = 0.0f;
                ls.elapsed = 0.0f;
                ls.duration = std::max(t.duration, 0.0f);
                break;
            }
        }
    }

    // Advance current (and next) state.
    const AnimState& current = states[ls.current];
    const float fromA = ls.time;
    ls.time += dt * current.speed / StateLength(current, clips);
    EvaluateState(current, ls.time, clips, skeleton, extract, pose);
    RootMotion ma = extract ? StateMotion(current, fromA, ls.time, clips) : RootMotion{};

    if (ls.next >= 0)
    {
        const AnimState& next = states[ls.next];
        const float fromB = ls.nextTime;
        ls.nextTime += dt * next.speed / StateLength(next, clips);
        ls.elapsed += dt;
        const float w = ls.duration > 0.0f ? std::clamp(ls.elapsed / ls.duration, 0.0f, 1.0f) : 1.0f;
        EvaluateState(next, ls.nextTime, clips, skeleton, extract, m_PoseB);
        for (size_t i = 0; i < pose.size() && i < m_PoseB.size(); ++i) pose[i] = Blend(pose[i], m_PoseB[i], w);
        if (extract)
        {
            const RootMotion mb = StateMotion(next, fromB, ls.nextTime, clips);
            ma.position = ma.position * (1.0f - w) + mb.position * w;
            ma.yaw = ma.yaw * (1.0f - w) + mb.yaw * w;
        }
        if (w >= 1.0f)
        {
            ls.current = ls.next;
            ls.time = ls.nextTime;
            ls.next = -1;
        }
    }
    motion = ma;
}

void AnimatorInstance::Update(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extractRootMotion, Pose& pose, RootMotion& motion)
{
    motion = {};
    if (!m_Controller || m_Layers.empty())
    {
        pose = skeleton.rest;
        return;
    }
    // Base layer: the whole body and the root motion.
    UpdateLayer(0, dt, clips, skeleton, extractRootMotion, pose, motion);

    // Upper layers blend over it inside their masks.
    for (int i = 1; i < static_cast<int>(m_Layers.size()); ++i)
    {
        RootMotion ignored;
        UpdateLayer(i, dt, clips, skeleton, true, m_LayerPose, ignored);
        const float w = m_Layers[i].weight;
        if (w <= 0.0f || m_LayerPose.size() != pose.size()) continue;
        const std::vector<uint8_t>& mask = Mask(i, skeleton);
        const AnimLayer& layer = m_Controller->layers[i];
        if (layer.blending == AnimLayerBlending::Override && layer.meshSpaceRotation)
        {
            // Top bones of the mask get the layer's model-space rotation; bones below keep the layer's local ones.
            PoseToModel(skeleton, pose, m_BaseModel);
            PoseToModel(skeleton, m_LayerPose, m_LayerModel);
            for (size_t b = 0; b < pose.size(); ++b)
            {
                if (!mask[b]) continue;
                const int parent = skeleton.parents[b];
                if (parent >= 0 && !mask[parent])
                {
                    const glm::quat baseModel = glm::normalize(glm::quat_cast(glm::mat3(m_BaseModel[b])));
                    const glm::quat layerModel = glm::normalize(glm::quat_cast(glm::mat3(m_LayerModel[b])));
                    const glm::quat target = glm::slerp(baseModel, glm::dot(baseModel, layerModel) < 0.0f ? -layerModel : layerModel, w);
                    const glm::quat parentModel = glm::normalize(glm::quat_cast(glm::mat3(m_BaseModel[parent])));
                    pose[b].r = glm::normalize(glm::inverse(parentModel) * target);
                    pose[b].t = glm::mix(pose[b].t, m_LayerPose[b].t, w);
                }
                else pose[b] = Blend(pose[b], m_LayerPose[b], w);
            }
        }
        else if (layer.blending == AnimLayerBlending::Override)
        {
            for (size_t b = 0; b < pose.size(); ++b)
                if (mask[b]) pose[b] = Blend(pose[b], m_LayerPose[b], w);
        }
        else
        {
            // Additive: the difference from the state's first frame is added on top (Unity's reference pose).
            const LayerState& ls = m_Layers[i];
            if (ls.current < 0) continue;
            EvaluateState(layer.states[ls.current], 0.0f, clips, skeleton, true, m_RefPose);
            for (size_t b = 0; b < pose.size(); ++b)
            {
                if (!mask[b]) continue;
                const glm::quat delta = glm::normalize(m_LayerPose[b].r * glm::inverse(m_RefPose[b].r));
                pose[b].r = glm::normalize(glm::slerp(glm::quat(1, 0, 0, 0), delta, w) * pose[b].r);
                pose[b].t += (m_LayerPose[b].t - m_RefPose[b].t) * w;
            }
        }
    }
}

void AnimatorInstance::SamplePreview(ClipLibrary& clips, const Skeleton& skeleton, Pose& pose)
{
    RootMotion motion;
    Update(0.0f, clips, skeleton, true, pose, motion);
}
