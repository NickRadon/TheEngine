#include "anim/AnimatorController.h"

#include "anim/BlendMask.h"
#include "anim/AnimationStream.h"

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
    const char* kInterruptNames[] = { "none", "current", "next", "current_next", "next_current" };

    template <size_t N>
    int IndexOf(const char* (&names)[N], const std::string& s)
    {
        for (size_t i = 0; i < N; ++i)
            if (s == names[i]) return static_cast<int>(i);
        return 0;
    }

    template <size_t N>
    const char* NameOf(const char* (&names)[N], int i)
    {
        return i >= 0 && i < static_cast<int>(N) ? names[i] : names[0];
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

bool AnimatorController::Save(std::ostream& out) const
{
    out << "TheEngineAnimator " << kCurrentVersion << "\n";
    for (const AnimParam& p : params)
        out << "param " << std::quoted(p.name) << ' ' << kTypeNames[static_cast<int>(p.type)] << ' ' << p.defaultValue << "\n";
    for (const AnimLayer& l : layers)
    {
        out << "layer " << std::quoted(l.name) << ' ' << l.weight << ' ' << kBlendNames[static_cast<int>(l.blending)];
        for (const std::string& bone : l.mask) out << ' ' << std::quoted(bone);
        if (l.meshSpaceRotation) out << " @meshspace";
        if (l.maskExact) out << " @exact";
        // Version 4 field; version 1-3 readers would take the path for a bone name.
        if (!l.maskAsset.empty()) out << " @mask " << std::quoted(l.maskAsset);
        out << "\n";
        if (!l.referenceClip.empty()) out << "reference " << std::quoted(l.referenceClip) << "\n";
        for (const AnimState& s : l.states)
        {
            out << "state " << std::quoted(s.name) << ' ' << kMotionNames[static_cast<int>(s.type)] << ' ' << std::quoted(s.clip) << ' '
                << std::quoted(s.paramX) << ' ' << std::quoted(s.paramY) << ' ' << s.speed << ' ' << s.loop << ' ' << s.position.x << ' '
                << s.position.y << "\n";
            for (const BlendChild& c : s.children)
                out << "  child " << std::quoted(c.clip) << ' ' << c.threshold << ' ' << c.position.x << ' ' << c.position.y << ' ' << c.speed << "\n";
            for (const AnimPoseOffset& offset : s.offsets)
                out << "  poseoffset " << std::quoted(offset.bone) << ' ' << offset.position.x << ' ' << offset.position.y << ' '
                    << offset.position.z << ' ' << offset.rotation.x << ' ' << offset.rotation.y << ' ' << offset.rotation.z << ' '
                    << offset.rotation.w << "\n";
        }
        for (const AnimTransition& t : l.transitions)
        {
            out << "transition " << std::quoted(t.from) << ' ' << std::quoted(t.to) << ' ' << t.hasExitTime << ' ' << t.exitTime << ' '
                << t.duration;
            // Version 3 fields; version 1/2 readers stop after the duration.
            out << " interrupt " << NameOf(kInterruptNames, static_cast<int>(t.interruption)) << " ordered " << t.ordered << "\n";
            for (const AnimCondition& c : t.conditions)
                out << "  condition " << std::quoted(c.param) << ' ' << kModeNames[static_cast<int>(c.mode)] << ' ' << c.threshold << "\n";
        }
        out << "default " << std::quoted(l.defaultState) << "\n";
        out << "entry " << l.entryPosition.x << ' ' << l.entryPosition.y << "\n";
        out << "any " << l.anyStatePosition.x << ' ' << l.anyStatePosition.y << "\n";
    }
    return static_cast<bool>(out);
}

bool AnimatorController::Save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;
    return Save(out);
}

std::string AnimatorController::ToString() const
{
    std::ostringstream out;
    Save(out);
    return out.str();
}

bool AnimatorController::Load(std::istream& file)
{
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
                else if (bone == "@exact") l.maskExact = true;
                else if (bone == "@mask") in >> std::quoted(l.maskAsset);
                else l.mask.push_back(bone);
            }
            c.layers.push_back(l);
        }
        else if (key == "reference") in >> std::quoted(layer().referenceClip);
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
        else if (key == "poseoffset" && !layer().states.empty())
        {
            AnimPoseOffset offset;
            in >> std::quoted(offset.bone) >> offset.position.x >> offset.position.y >> offset.position.z >>
                offset.rotation.x >> offset.rotation.y >> offset.rotation.z >> offset.rotation.w;
            offset.rotation = glm::normalize(offset.rotation);
            layer().states.back().offsets.push_back(offset);
        }
        else if (key == "transition")
        {
            AnimTransition t;
            in >> std::quoted(t.from) >> std::quoted(t.to) >> t.hasExitTime >> t.exitTime >> t.duration;
            // Optional version 3 tail: "interrupt <mode> ordered <0|1>".
            std::string tag;
            while (in >> tag)
            {
                if (tag == "interrupt")
                {
                    std::string mode;
                    in >> mode;
                    t.interruption = static_cast<AnimInterruption>(IndexOf(kInterruptNames, mode));
                }
                else if (tag == "ordered") in >> t.ordered;
            }
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
    // Resolve the blend mask assets now so validation and the first pose use their bones.
    for (AnimLayer& l : c.layers) l.RefreshMaskAsset();
    *this = std::move(c);
    return true;
}

bool AnimatorController::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file) return false;
    return Load(file);
}

bool AnimatorController::LoadString(const std::string& text)
{
    std::istringstream in(text);
    return Load(in);
}

int AnimatorController::FindParam(const std::string& name) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == name) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
std::vector<AnimIssue> AnimatorController::Validate(ClipLibrary* clips, const Skeleton* skeleton) const
{
    std::vector<AnimIssue> issues;
    auto add = [&](int layer, int state, int transition, std::string text) {
        issues.push_back({ layer, state, transition, std::move(text) });
    };
    auto paramUsable = [&](const std::string& name) {
        const int p = FindParam(name);
        return p >= 0 && (params[p].type == AnimParamType::Float || params[p].type == AnimParamType::Int);
    };

    for (size_t li = 0; li < layers.size(); ++li)
    {
        const AnimLayer& l = layers[li];
        const int layer = static_cast<int>(li);
        if (l.states.empty())
        {
            add(layer, -1, -1, "Layer '" + l.name + "' has no states.");
            continue;
        }
        if (l.defaultState.empty()) add(layer, -1, -1, "Layer '" + l.name + "' has no default state.");
        else if (l.FindState(l.defaultState) < 0)
            add(layer, -1, -1, "Layer '" + l.name + "': default state '" + l.defaultState + "' does not exist.");
        if (!l.referenceClip.empty() && clips && !clips->Get(l.referenceClip))
            add(layer, -1, -1, "Layer '" + l.name + "': reference clip '" + l.referenceClip + "' could not be loaded.");

        // States
        for (size_t si = 0; si < l.states.size(); ++si)
        {
            const AnimState& s = l.states[si];
            const int state = static_cast<int>(si);
            if (si != static_cast<size_t>(l.FindState(s.name)))
                add(layer, state, -1, "Layer '" + l.name + "': duplicate state name '" + s.name + "'.");
            if (skeleton)
                for (const AnimPoseOffset& offset : s.offsets)
                    if (skeleton->Find(offset.bone) < 0)
                        add(layer, state, -1, "State '" + s.name + "': pose offset bone '" + offset.bone + "' is not in the rig.");
            if (s.type == AnimMotionType::Clip)
            {
                if (s.clip.empty()) add(layer, state, -1, "State '" + s.name + "' has no clip: it plays the rest pose.");
                else if (clips && !clips->Get(s.clip))
                    add(layer, state, -1, "State '" + s.name + "': clip '" + s.clip + "' could not be loaded (missing file or take).");
            }
            else
            {
                const bool twoD = s.type == AnimMotionType::BlendTree2D;
                if (s.paramX.empty()) add(layer, state, -1, "State '" + s.name + "': blend tree has no parameter.");
                else if (!paramUsable(s.paramX))
                    add(layer, state, -1, "State '" + s.name + "': parameter '" + s.paramX + "' is missing or is not a float/int.");
                if (twoD && !s.paramY.empty() && !paramUsable(s.paramY))
                    add(layer, state, -1, "State '" + s.name + "': parameter '" + s.paramY + "' is missing or is not a float/int.");
                if (s.children.empty()) add(layer, state, -1, "State '" + s.name + "': blend tree has no motions.");
                for (size_t ci = 0; ci < s.children.size(); ++ci)
                {
                    const BlendChild& ch = s.children[ci];
                    if (ch.clip.empty()) add(layer, state, -1, "State '" + s.name + "': motion " + std::to_string(ci + 1) + " has no clip.");
                    else if (clips && !clips->Get(ch.clip))
                        add(layer, state, -1, "State '" + s.name + "': clip '" + ch.clip + "' could not be loaded (missing file or take).");
                    if (!twoD)
                        for (size_t cj = 0; cj < ci; ++cj)
                            if (s.children[cj].threshold == ch.threshold)
                                add(layer, state, -1, "State '" + s.name + "': two motions share threshold " +
                                                          std::to_string(ch.threshold) + " (1D blend is ambiguous).");
                }
            }
        }

        // Transitions
        for (size_t ti = 0; ti < l.transitions.size(); ++ti)
        {
            const AnimTransition& t = l.transitions[ti];
            const int transition = static_cast<int>(ti);
            const bool fromAny = t.from == AnimLayer::kAnyState;
            if (!fromAny && l.FindState(t.from) < 0)
                add(layer, -1, transition, "Transition from missing state '" + t.from + "'.");
            if (l.FindState(t.to) < 0) add(layer, -1, transition, "Transition to missing state '" + t.to + "'.");
            if (!t.hasExitTime && t.conditions.empty())
                add(layer, -1, transition, "Transition " + t.from + " -> " + t.to + " has no exit time and no conditions (it never fires).");
            for (const AnimCondition& c : t.conditions)
                if (FindParam(c.param) < 0)
                    add(layer, -1, transition, "Transition " + t.from + " -> " + t.to + " uses unknown parameter '" + c.param + "'.");
        }

        // Reachability: everything the default state can walk to.
        std::vector<uint8_t> reachable(l.states.size(), 0);
        std::vector<int> stack;
        if (const int d = l.FindState(l.defaultState); d >= 0) { reachable[d] = 1; stack.push_back(d); }
        while (!stack.empty())
        {
            const std::string name = l.states[stack.back()].name;
            stack.pop_back();
            for (const AnimTransition& t : l.transitions)
            {
                const bool fromAny = t.from == AnimLayer::kAnyState;
                if (!fromAny && t.from != name) continue;
                const int to = l.FindState(t.to);
                if (to >= 0 && !reachable[to]) { reachable[to] = 1; stack.push_back(to); }
            }
        }
        for (size_t si = 0; si < l.states.size(); ++si)
            if (!reachable[si])
                add(layer, static_cast<int>(si), -1, "State '" + l.states[si].name + "' is unreachable from the default state.");

        // Layer settings
        if (!l.maskAsset.empty() && l.maskAssetMissing)
            add(layer, -1, -1, "Layer '" + l.name + "': blend mask '" + l.maskAsset + "' could not be loaded.");
        for (const std::string& bone : l.EffectiveMask())
            if (skeleton && skeleton->Find(bone) < 0)
                add(layer, -1, -1, "Layer '" + l.name + "': mask bone '" + bone + "' is not in the rig.");
    }
    return issues;
}

int AnimLayer::FindState(const std::string& n) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == n) return static_cast<int>(i);
    return -1;
}

// No mask at all means the whole body (the inline list has always worked that way). An assigned mask
// asset means "only these bones": an empty or unreadable one therefore drives nothing, so a deleted
// .mask file cannot silently turn an upper-body layer into a full-body one.
bool AnimLayer::DrivesWholeBody() const
{
    if (!maskAsset.empty()) return false;
    return mask.empty();
}

// (Re)reads the .mask file behind maskAsset when it is new or changed on disk; true when the
// resolved bones changed (the AnimationSystem then bumps the controller version).
bool AnimLayer::RefreshMaskAsset()
{
    const std::vector<std::string> oldBones = std::move(maskAssetBones);
    const bool oldMissing = maskAssetMissing;
    const auto oldStamp = maskAssetStamp;
    maskAssetBones.clear();
    maskAssetMissing = false;
    maskAssetStamp = {};
    if (maskAsset.empty())
        return !oldBones.empty() || oldMissing;

    BlendMask mask;
    maskAssetMissing = !mask.Load(maskAsset);
    if (!maskAssetMissing) maskAssetBones = std::move(mask.bones);
    std::error_code ec;
    maskAssetStamp = std::filesystem::last_write_time(maskAsset, ec);
    if (ec) maskAssetStamp = {};
    return maskAssetBones != oldBones || maskAssetMissing != oldMissing || maskAssetStamp != oldStamp;
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

bool AnimatorInstance::SetParamDamped(const std::string& name, float target, float dampTime, float dt)
{
    if (!m_Controller) return false;
    const int i = m_Controller->FindParam(name);
    if (i < 0 || i >= static_cast<int>(m_Values.size())) return false;
    if (dampTime <= 0.0f || dt <= 0.0f) return SetParam(name, target);
    const AnimParamType type = m_Controller->params[i].type;
    if (type == AnimParamType::Bool || type == AnimParamType::Trigger) return SetParam(name, target);
    // Half of Unity's damp time curve: one exponential step per call, no stored target.
    const float t = 1.0f - std::exp(-dt / dampTime);
    float value = m_Values[i] + (target - m_Values[i]) * t;
    if (type == AnimParamType::Int) value = std::round(value);
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
    // A state whose clips are missing still advances, at one second per normalized unit: without this
    // its length would collapse to a millisecond and its normalized time (and exit times) would race.
    if (length <= 0.0f) return 1.0f;
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
    if (!state.offsets.empty())
    {
        RootMotion ignored;
        AnimationStream stream(skeleton, out, ignored);
        for (const AnimPoseOffset& offset : state.offsets)
        {
            const BoneHandle bone = stream.Bind(offset.bone);
            if (!stream.Valid(bone)) continue;
            const glm::mat4 current = stream.Model(bone);
            const glm::mat4 adjusted = glm::translate(glm::mat4(1.0f), offset.position) * glm::mat4_cast(offset.rotation) * current;
            stream.SetModel(bone, adjusted);
        }
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
        if (i >= 0 && m_Controller->params[i].type == AnimParamType::Trigger) m_ConsumedTriggers.push_back(i);
    }
}

const std::vector<uint8_t>& AnimatorInstance::Mask(int index, const Skeleton& skeleton)
{
    LayerState& l = m_Layers[index];
    if (l.maskSkeleton == &skeleton && l.mask.size() == skeleton.names.size()) return l.mask;
    const AnimLayer& layer = m_Controller->layers[index];
    l.mask.assign(skeleton.names.size(), layer.DrivesWholeBody() ? 1 : 0);
    // A bone is in a subtree mask when it or one of its ancestors is listed (parents precede children).
    for (const std::string& bone : layer.EffectiveMask())
        if (const int b = skeleton.Find(bone); b >= 0) l.mask[b] = 1;
    if (!layer.maskExact)
        for (size_t i = 0; i < skeleton.names.size(); ++i)
            if (skeleton.parents[i] >= 0 && l.mask[skeleton.parents[i]]) l.mask[i] = 1;
    l.maskSkeleton = &skeleton;
    return l.mask;
}

// Has the normalized time of `state` reached `exitTime` this frame? Looping states fire every lap,
// non-looping ones once the time grows past it (a state that does not advance cannot cross).
bool AnimatorInstance::ExitCrossed(const AnimState& state, float prev, float after, float exitTime)
{
    if (state.loop && exitTime < 1.0f)
    {
        const float a = prev - std::floor(prev), b = a + (after - prev);
        return (a < exitTime && b >= exitTime) || (b >= 1.0f + exitTime);
    }
    return after >= exitTime && after > prev;
}

int AnimatorInstance::FindTransition(const AnimLayer& layer, LayerState& ls, float curAfter, float nextAfter)
{
    const std::vector<AnimState>& states = layer.states;
    const bool running = ls.next >= 0 && ls.transition >= 0 && ls.transition < static_cast<int>(layer.transitions.size());
    const AnimInterruption mode = running ? layer.transitions[ls.transition].interruption : AnimInterruption::None;
    if (running && mode == AnimInterruption::None) return -1; // a transition always finishes
    const bool ordered = running && layer.transitions[ls.transition].ordered;
    const bool fromCurrent = !running || mode == AnimInterruption::Current || mode == AnimInterruption::CurrentThenNext ||
                             mode == AnimInterruption::NextThenCurrent;
    const bool fromNext = !running || mode == AnimInterruption::Next || mode == AnimInterruption::CurrentThenNext ||
                          mode == AnimInterruption::NextThenCurrent;
    const std::string currentName = states[ls.current].name;
    const std::string nextName = ls.next >= 0 ? states[ls.next].name : std::string();

    // Is transition i ready this frame? Exit times are measured on the state the transition leaves.
    auto ready = [&](int i, int& target) -> bool {
        const AnimTransition& t = layer.transitions[i];
        if (running && i == ls.transition) return false; // don't restart the running transition
        const bool fromAny = t.from == AnimLayer::kAnyState;
        target = layer.FindState(t.to);
        if (target < 0) return false;
        if (fromAny && (target == ls.current || target == ls.next)) return false;
        int source = ls.current;
        if (!fromAny)
        {
            if (t.from == currentName)
            {
                if (!fromCurrent) return false;
            }
            else if (ls.next >= 0 && t.from == nextName && fromNext) source = ls.next;
            else return false;
        }
        const float prev = source == ls.current ? ls.time : ls.nextTime;
        const float after = source == ls.current ? curAfter : nextAfter;
        if (t.hasExitTime && !ExitCrossed(states[source], prev, after, t.exitTime)) return false;
        if (!t.hasExitTime && t.conditions.empty()) return false; // would fire every frame
        return ConditionsMet(t);
    };

    // Candidates are grouped: Any State first, then the states the source setting allows. With
    // "ordered interruption" the layer's own list order decides instead.
    std::vector<std::vector<int>> groups;
    const auto collect = [&](const std::string& name, bool any) {
        std::vector<int> g;
        for (int i = 0; i < static_cast<int>(layer.transitions.size()); ++i)
            if ((layer.transitions[i].from == AnimLayer::kAnyState) == any && (any || layer.transitions[i].from == name)) g.push_back(i);
        if (!g.empty()) groups.push_back(std::move(g));
    };
    if (ordered && running)
    {
        std::vector<int> g;
        for (int i = 0; i < static_cast<int>(layer.transitions.size()); ++i)
        {
            const AnimTransition& t = layer.transitions[i];
            const bool eligible = t.from == AnimLayer::kAnyState || (fromCurrent && t.from == currentName) ||
                                  (fromNext && ls.next >= 0 && t.from == nextName);
            if (eligible) g.push_back(i);
        }
        if (!g.empty()) groups.push_back(std::move(g));
    }
    else
    {
        collect({}, true); // Any State
        if (mode == AnimInterruption::NextThenCurrent)
        {
            if (ls.next >= 0) collect(nextName, false);
            collect(currentName, false);
        }
        else
        {
            collect(currentName, false);
            if (mode != AnimInterruption::None && ls.next >= 0) collect(nextName, false);
        }
    }
    for (const std::vector<int>& g : groups)
        for (const int i : g)
        {
            int target = -1;
            if (ready(i, target)) return i;
        }
    return -1;
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
    const auto advance = [&](int s, float time) { return time + dt * states[s].speed / StateLength(states[s], clips); };

    // End-of-frame times before a transition starts; exit times are checked against them.
    const float curAfter = advance(ls.current, ls.time);
    const float nextAfter = ls.next >= 0 ? advance(ls.next, ls.nextTime) : ls.nextTime;

    // ---- Pick a transition (a running one may be interrupted) -----------------
    const int chosen = FindTransition(layer, ls, curAfter, nextAfter);
    if (chosen >= 0)
    {
        const AnimTransition& t = layer.transitions[chosen];
        const int target = layer.FindState(t.to);
        if (ls.next >= 0)
        {
            // Freeze the interrupted blend at its current weights: it keeps playing as the new "from"
            // side (no pose pop) and its states keep driving the root motion underneath.
            const float wOld = ls.duration > 0.0f ? std::clamp(ls.elapsed / ls.duration, 0.0f, 1.0f) : 1.0f;
            std::vector<MotionSource> kept;
            const auto push = [&](int state, float time, float base) {
                if (base > 1e-5f && state >= 0) kept.push_back({ state, time, base });
            };
            if (ls.frozen)
                for (const MotionSource& s : ls.sources) push(s.state, s.time, s.base * (1.0f - wOld));
            else
                push(ls.current, ls.time, 1.0f - wOld);
            push(ls.next, ls.nextTime, wOld);
            if (kept.empty()) push(ls.current, ls.time, 1.0f);
            // Long interruption chains merge the lightest sources so evaluation stays bounded.
            while (kept.size() > 4)
            {
                size_t light = 0;
                for (size_t i = 1; i < kept.size(); ++i)
                    if (kept[i].base < kept[light].base) light = i;
                const float dropped = kept[light].base;
                kept.erase(kept.begin() + static_cast<long>(light));
                const float rest = std::max(1.0f - dropped, 1e-6f);
                for (MotionSource& s : kept) s.base /= rest;
            }
            float sum = 0.0f;
            for (const MotionSource& s : kept) sum += s.base;
            for (MotionSource& s : kept) s.base /= std::max(sum, 1e-6f);
            ls.sources = std::move(kept);
            ls.frozen = true;
            // The logical source of the new transition is the state it leaves.
            if (t.from != AnimLayer::kAnyState && t.from == states[ls.next].name)
            {
                ls.current = ls.next;
                ls.time = ls.nextTime;
            }
        }
        else
        {
            ls.frozen = false;
            ls.sources.clear();
        }
        ConsumeTriggers(t);
        ls.next = target;
        ls.nextTime = 0.0f;
        ls.elapsed = 0.0f;
        ls.duration = std::max(t.duration, 0.0f);
        ls.transition = chosen;
    }

    // ---- Advance -------------------------------------------------------------
    const int fromState = ls.current;
    const float fromTime = ls.time;
    ls.time = advance(fromState, ls.time);
    float nextTimeFrom = ls.nextTime;
    if (ls.next >= 0)
    {
        ls.nextTime = advance(ls.next, ls.nextTime);
        ls.elapsed += dt;
    }
    std::vector<glm::vec2> spans; // (from, to) of every frozen source this frame (root motion only)
    if (ls.frozen)
    {
        if (extract) spans.reserve(ls.sources.size());
        for (MotionSource& s : ls.sources)
        {
            const float from = s.time;
            s.time = advance(s.state, s.time);
            if (extract) spans.push_back({ from, s.time });
        }
    }

    // ---- Pose ----------------------------------------------------------------
    const float w = ls.next >= 0 ? (ls.duration > 0.0f ? std::clamp(ls.elapsed / ls.duration, 0.0f, 1.0f) : 1.0f) : 0.0f;
    if (ls.frozen)
    {
        // Progressive blend of the frozen sources (weights add up to 1).
        float accumulated = 0.0f;
        for (size_t i = 0; i < ls.sources.size(); ++i)
        {
            EvaluateState(states[ls.sources[i].state], ls.sources[i].time, clips, skeleton, extract, m_PoseC);
            if (i == 0)
                pose = m_PoseC;
            else
            {
                accumulated += ls.sources[i - 1].base;
                const float t = ls.sources[i].base / std::max(accumulated + ls.sources[i].base, 1e-6f);
                for (size_t b = 0; b < pose.size() && b < m_PoseC.size(); ++b) pose[b] = Blend(pose[b], m_PoseC[b], t);
            }
        }
    }
    else
        EvaluateState(states[ls.current], ls.time, clips, skeleton, extract, pose);
    if (ls.next >= 0)
    {
        EvaluateState(states[ls.next], ls.nextTime, clips, skeleton, extract, m_PoseB);
        for (size_t b = 0; b < pose.size() && b < m_PoseB.size(); ++b) pose[b] = Blend(pose[b], m_PoseB[b], w);
    }

    // ---- Root motion ---------------------------------------------------------
    if (extract)
    {
        if (ls.frozen)
        {
            for (size_t i = 0; i < ls.sources.size(); ++i)
            {
                const RootMotion m = StateMotion(states[ls.sources[i].state], spans[i].x, spans[i].y, clips);
                motion.position += m.position * ls.sources[i].base;
                motion.yaw += m.yaw * ls.sources[i].base;
            }
        }
        else
            motion = StateMotion(states[fromState], fromTime, ls.time, clips);
        if (ls.next >= 0)
        {
            const RootMotion mb = StateMotion(states[ls.next], nextTimeFrom, ls.nextTime, clips);
            motion.position = motion.position * (1.0f - w) + mb.position * w;
            motion.yaw = motion.yaw * (1.0f - w) + mb.yaw * w;
        }
    }

    // ---- Finish the transition ----------------------------------------------
    if (ls.next >= 0 && w >= 1.0f)
    {
        ls.current = ls.next;
        ls.time = ls.nextTime;
        ls.next = -1;
        ls.transition = -1;
        ls.frozen = false;
        ls.sources.clear();
    }
}

void AnimatorInstance::Update(float dt, ClipLibrary& clips, const Skeleton& skeleton, bool extractRootMotion, Pose& pose, RootMotion& motion)
{
    motion = {};
    if (!m_Controller || m_Layers.empty())
    {
        pose = skeleton.rest;
        return;
    }
    // Every layer sees the same trigger snapshot, including zero-weight layers. Consuming a
    // shared action on the base layer must not prevent an upper-body layer from entering it.
    m_ConsumedTriggers.clear();
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
                    // IK targets often sit outside the deforming spine hierarchy. Their model-space
                    // position must come from the FP clip as well, or walking hips drag the weapon
                    // and hand targets even though these bones are inside the upper-body mask.
                    if (skeleton.names[b].rfind("ik_", 0) == 0)
                    {
                        const glm::vec3 basePosition(m_BaseModel[b][3]);
                        const glm::vec3 layerPosition(m_LayerModel[b][3]);
                        const glm::vec3 modelPosition = glm::mix(basePosition, layerPosition, w);
                        pose[b].t = glm::vec3(glm::inverse(m_BaseModel[parent]) * glm::vec4(modelPosition, 1.0f));
                    }
                    else pose[b].t = glm::mix(pose[b].t, m_LayerPose[b].t, w);
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
            // Additive: use an explicit neutral clip when supplied, so a held pose offset does not
            // cancel itself against the state's first frame.
            const LayerState& ls = m_Layers[i];
            if (ls.current < 0) continue;
            if (const AnimationClip* reference = layer.referenceClip.empty() ? nullptr : clips.Get(layer.referenceClip))
                SampleClip(*reference, clips.Binding(reference, &skeleton), skeleton, 0.0f, true, m_RefPose);
            else EvaluateState(layer.states[ls.current], 0.0f, clips, skeleton, true, m_RefPose);
            for (size_t b = 0; b < pose.size(); ++b)
            {
                if (!mask[b]) continue;
                const glm::quat delta = glm::normalize(m_LayerPose[b].r * glm::inverse(m_RefPose[b].r));
                pose[b].r = glm::normalize(glm::slerp(glm::quat(1, 0, 0, 0), delta, w) * pose[b].r);
                pose[b].t += (m_LayerPose[b].t - m_RefPose[b].t) * w;
            }
        }
    }
    for (int trigger : m_ConsumedTriggers) m_Values[trigger] = 0.0f;
}

void AnimatorInstance::SamplePreview(ClipLibrary& clips, const Skeleton& skeleton, Pose& pose)
{
    RootMotion motion;
    Update(0.0f, clips, skeleton, true, pose, motion);
}
