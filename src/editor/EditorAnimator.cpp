// Animator window: node-based editor for Animator Controllers (Unity's Window > Animation > Animator).
#include "editor/Editor.h"

#include "core/Log.h"
#include "editor/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    constexpr float kNodeW = 170.0f, kNodeH = 42.0f;
    constexpr const char* kAny = AnimatorController::kAnyState;

    ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

    bool Contains(const std::string& text, const char* filter)
    {
        if (!filter || !*filter) return true;
        std::string a = text, b = filter;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a.find(b) != std::string::npos;
    }

    float SegmentDistance(ImVec2 p, ImVec2 a, ImVec2 b)
    {
        const ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        const float t = len2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
        const float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    std::string ClipLabel(const std::string& ref)
    {
        if (ref.empty()) return "None (Motion)";
        const size_t hash = ref.rfind('#');
        return hash != std::string::npos ? ref.substr(hash + 1) : fs::path(ref).stem().string();
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void Editor::OpenAnimatorController(const std::string& path)
{
    m_AnimCtrlPath = path;
    m_AnimSelState.clear();
    m_AnimSelTransition = -1;
    m_AnimLinkFrom.clear();
    m_FocusAnimator = true;
    m_AnimFrameRequest = true; // fit the graph once the canvas size is known
}

AnimatorController* Editor::EditedController()
{
    return m_AnimCtrlPath.empty() ? nullptr : m_Animation.Controller(m_AnimCtrlPath);
}

void Editor::SaveEditedController()
{
    AnimatorController* c = EditedController();
    if (!c) return;
    if (c->Save(m_AnimCtrlPath)) m_Animation.ControllerEdited(m_AnimCtrlPath);
    else LOG_ERROR("Could not save %s", m_AnimCtrlPath.c_str());
    m_AnimCtrlDirty = false;
}

const std::vector<std::string>& Editor::AnimationFiles()
{
    // All FBX files under Assets (refreshed every few seconds).
    if (m_AnimFilesTimer <= 0.0f)
    {
        m_AnimFilesTimer = 3.0f;
        m_AnimFiles.clear();
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator("Assets", ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".fbx") m_AnimFiles.push_back(it->path().generic_string());
        }
        std::sort(m_AnimFiles.begin(), m_AnimFiles.end());
    }
    return m_AnimFiles;
}

// Motion field: button showing the clip, a searchable popup of FBX files, and a drop target for .fbx assets.
bool Editor::ClipField(const char* id, std::string& clip)
{
    bool changed = false;
    ImGui::PushID(id);
    const float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(ClipLabel(clip).c_str(), ImVec2(w, 0)))
    {
        m_AnimClipFilter[0] = 0;
        ImGui::OpenPopup("ClipPicker");
    }
    if (!clip.empty()) ImGui::SetItemTooltip("%s", clip.c_str());
    if (ImGui::BeginDragDropTarget())
    {
        std::string asset;
        if (AcceptAssetDrop(".fbx", asset) || AcceptAssetDrop(".FBX", asset)) { clip = asset; changed = true; }
        ImGui::EndDragDropTarget();
    }
    ImGui::SetNextWindowSize(ImVec2(420, 360));
    if (ImGui::BeginPopup("ClipPicker"))
    {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Search clips (e.g. jog fwd)", m_AnimClipFilter, sizeof(m_AnimClipFilter));
        ImGui::BeginChild("##clips");
        if (ImGui::Selectable("None")) { clip.clear(); changed = true; ImGui::CloseCurrentPopup(); }
        // Space separated words must all match.
        std::vector<std::string> words;
        {
            std::string w2;
            for (const char* p = m_AnimClipFilter; ; ++p)
            {
                if (*p == ' ' || *p == 0) { if (!w2.empty()) words.push_back(w2); w2.clear(); if (!*p) break; }
                else w2 += *p;
            }
        }
        for (const std::string& f : AnimationFiles())
        {
            bool match = true;
            for (const std::string& word : words) match = match && Contains(fs::path(f).filename().string(), word.c_str());
            if (!match) continue;
            if (ImGui::Selectable(fs::path(f).stem().string().c_str(), f == clip)) { clip = f; changed = true; ImGui::CloseCurrentPopup(); }
            ImGui::SetItemTooltip("%s", f.c_str());
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
void Editor::DrawAnimator()
{
    if (m_SceneDockId && !m_AnimatorDocked)
    {
        ImGui::SetNextWindowDockID(m_SceneDockId, ImGuiCond_FirstUseEver);
        m_AnimatorDocked = true;
    }
    if (m_FocusAnimator)
    {
        ImGui::SetNextWindowFocus();
        m_FocusAnimator = false;
    }
    if (!ImGui::Begin("Animator"))
    {
        ImGui::End();
        return;
    }
    m_AnimFilesTimer -= ImGui::GetIO().DeltaTime;

    // Follow the selection: an object with an Animator shows its controller (and its live state in play mode).
    EntityId live = kNullEntity;
    if (const Entity* sel = m_Scene.Find(ActiveEntity()); sel && sel->animator.enabled)
    {
        live = sel->id;
        if (!sel->animator.controller.empty() && sel->animator.controller != m_AnimCtrlPath) OpenAnimatorController(sel->animator.controller);
    }
    if (!live)
        for (const Entity& e : m_Scene.entities)
            if (e.animator.enabled && e.animator.controller == m_AnimCtrlPath) { live = e.id; break; }
    AnimatorInstance* instance = m_Playing && live ? m_Animation.Instance(live) : nullptr;

    AnimatorController* c = EditedController();
    if (!c)
    {
        ImGui::Spacing();
        ImGui::TextDisabled(m_AnimCtrlPath.empty() ? "Select an object with an Animator, or double-click an Animator Controller asset."
                                                   : "Could not load the controller.");
        if (ImGui::Button("Create Animator Controller"))
        {
            const std::string path = CreateAsset("Assets/Animators", "controller");
            if (!path.empty()) OpenAnimatorController(path);
        }
        ImGui::End();
        return;
    }

    // Toolbar
    ImGui::TextUnformatted(fs::path(m_AnimCtrlPath).stem().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", m_AnimCtrlPath.c_str(), instance ? "   (live)" : "");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 190);
    if (ImGui::SmallButton("Frame All")) m_AnimFrameRequest = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Project")) SelectAsset(m_AnimCtrlPath);
    ImGui::Separator();

    const float leftW = 210.0f, rightW = 300.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    // ---------------- Parameters ----------------
    ImGui::BeginChild("##params", ImVec2(leftW, avail.y), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Parameters");
    ImGui::SameLine(leftW - 36);
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("AddParam");
    if (ImGui::BeginPopup("AddParam"))
    {
        const char* names[] = { "Float", "Int", "Bool", "Trigger" };
        for (int i = 0; i < 4; ++i)
            if (ImGui::MenuItem(names[i]))
            {
                AnimParam p;
                p.type = static_cast<AnimParamType>(i);
                p.name = "New " + std::string(names[i]);
                for (int n = 1; c->FindParam(p.name) >= 0; ++n) p.name = "New " + std::string(names[i]) + " " + std::to_string(n);
                c->params.push_back(p);
                m_AnimCtrlDirty = true;
            }
        ImGui::EndPopup();
    }
    ImGui::Separator();
    int removeParam = -1;
    for (size_t i = 0; i < c->params.size(); ++i)
    {
        AnimParam& p = c->params[i];
        ImGui::PushID(static_cast<int>(i));
        char name[64];
        std::snprintf(name, sizeof(name), "%s", p.name.c_str());
        ImGui::SetNextItemWidth(leftW - 100);
        if (ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && name[0] && c->FindParam(name) < 0)
        {
            // Keep conditions and blend trees pointing at the renamed parameter.
            for (AnimTransition& t : c->transitions)
                for (AnimCondition& cond : t.conditions)
                    if (cond.param == p.name) cond.param = name;
            for (AnimState& s : c->states)
            {
                if (s.paramX == p.name) s.paramX = name;
                if (s.paramY == p.name) s.paramY = name;
            }
            p.name = name;
            m_AnimCtrlDirty = true;
        }
        if (ImGui::BeginPopupContextItem("ParamContext"))
        {
            if (ImGui::MenuItem("Delete")) removeParam = static_cast<int>(i);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        // Live value in play mode, default value otherwise.
        float value = instance ? instance->GetParam(p.name) : p.defaultValue;
        bool edited = false;
        switch (p.type)
        {
        case AnimParamType::Float: edited = ImGui::DragFloat("##v", &value, 0.01f, 0.0f, 0.0f, "%.2f"); break;
        case AnimParamType::Int:
        {
            int v = static_cast<int>(value);
            if ((edited = ImGui::DragInt("##v", &v))) value = static_cast<float>(v);
            break;
        }
        case AnimParamType::Bool:
        {
            bool v = value != 0.0f;
            if ((edited = ImGui::Checkbox("##v", &v))) value = v ? 1.0f : 0.0f;
            break;
        }
        case AnimParamType::Trigger:
        {
            const bool set = value != 0.0f;
            if (set) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.55f, 0.1f, 1.0f));
            if (ImGui::Button(set ? "set" : "trigger", ImVec2(-FLT_MIN, 0))) { value = 1.0f; edited = true; }
            if (set) ImGui::PopStyleColor();
            break;
        }
        }
        if (edited)
        {
            if (instance) instance->SetParam(p.name, value);
            else if (p.type != AnimParamType::Trigger)
            {
                p.defaultValue = value;
                m_AnimCtrlDirty = true;
            }
        }
        ImGui::PopID();
    }
    if (removeParam >= 0)
    {
        c->params.erase(c->params.begin() + removeParam);
        m_AnimCtrlDirty = true;
    }
    if (c->params.empty()) ImGui::TextDisabled("No parameters. Use + to add one.");
    ImGui::EndChild();

    ImGui::SameLine();
    DrawAnimatorGraph(*c, instance, ImVec2(avail.x - leftW - rightW - 16, avail.y));
    ImGui::SameLine();

    // ---------------- Selection properties ----------------
    ImGui::BeginChild("##props", ImVec2(rightW, avail.y), ImGuiChildFlags_Borders);
    DrawAnimatorSelection(*c, instance);
    ImGui::EndChild();

    if (m_AnimCtrlDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) SaveEditedController();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Graph canvas
// ---------------------------------------------------------------------------
void Editor::DrawAnimatorGraph(AnimatorController& c, AnimatorInstance* instance, ImVec2 size)
{
    ImGui::BeginChild("##graph", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##canvas", canvas, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    // Frame All: fit every node into the canvas (zoom out if needed, never above 1).
    if (m_AnimFrameRequest && canvas.x > 50 && canvas.y > 50)
    {
        m_AnimFrameRequest = false;
        glm::vec2 mn = glm::min(c.entryPosition, c.anyStatePosition), mx = glm::max(c.entryPosition, c.anyStatePosition);
        for (const AnimState& st : c.states) { mn = glm::min(mn, st.position); mx = glm::max(mx, st.position); }
        mx += glm::vec2(kNodeW, kNodeH);
        const glm::vec2 extent = mx - mn + glm::vec2(80.0f);
        m_AnimZoom = std::clamp(std::min(canvas.x / extent.x, canvas.y / extent.y), 0.3f, 1.0f);
        const glm::vec2 center = (mn + mx) * 0.5f;
        m_AnimPan = ImVec2(canvas.x * 0.5f / m_AnimZoom - center.x, canvas.y * 0.5f / m_AnimZoom - center.y);
    }

    auto toScreen = [&](glm::vec2 p) { return ImVec2(origin.x + (p.x + m_AnimPan.x) * m_AnimZoom, origin.y + (p.y + m_AnimPan.y) * m_AnimZoom); };
    auto toCanvas = [&](ImVec2 s) { return glm::vec2((s.x - origin.x) / m_AnimZoom - m_AnimPan.x, (s.y - origin.y) / m_AnimZoom - m_AnimPan.y); };

    // Pan (MMB or Alt+LMB) and zoom (wheel, around the mouse).
    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
    {
        m_AnimPan.x += io.MouseDelta.x / m_AnimZoom;
        m_AnimPan.y += io.MouseDelta.y / m_AnimZoom;
    }
    if (hovered && io.MouseWheel != 0.0f)
    {
        const glm::vec2 before = toCanvas(mouse);
        m_AnimZoom = std::clamp(m_AnimZoom * (io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f), 0.3f, 2.0f);
        const glm::vec2 after = toCanvas(mouse);
        m_AnimPan.x += after.x - before.x;
        m_AnimPan.y += after.y - before.y;
    }

    // Grid
    dl->PushClipRect(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), true);
    dl->AddRectFilled(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), Col(40, 40, 40));
    for (int level = 0; level < 2; ++level)
    {
        const float step = (level == 0 ? 20.0f : 100.0f) * m_AnimZoom;
        const ImU32 col = level == 0 ? Col(46, 46, 46) : Col(34, 34, 34);
        const float ox = std::fmod(m_AnimPan.x * m_AnimZoom, step), oy = std::fmod(m_AnimPan.y * m_AnimZoom, step);
        for (float x = ox; x < canvas.x; x += step) dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + canvas.y), col);
        for (float y = oy; y < canvas.y; y += step) dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + canvas.x, origin.y + y), col);
    }

    // Node rectangles by name ("Entry", "Any State" and states).
    const std::string kEntry = "\x01Entry";
    auto nodePos = [&](const std::string& name) -> glm::vec2 {
        if (name == kEntry) return c.entryPosition;
        if (name == kAny) return c.anyStatePosition;
        const int i = c.FindState(name);
        return i >= 0 ? c.states[i].position : glm::vec2(0.0f);
    };
    auto nodeCenter = [&](const std::string& name) {
        const ImVec2 p = toScreen(nodePos(name));
        return ImVec2(p.x + kNodeW * m_AnimZoom * 0.5f, p.y + kNodeH * m_AnimZoom * 0.5f);
    };
    auto nodeAt = [&](ImVec2 s) -> std::string {
        for (int i = static_cast<int>(c.states.size()) - 1; i >= 0; --i)
        {
            const ImVec2 p = toScreen(c.states[i].position);
            if (s.x >= p.x && s.y >= p.y && s.x <= p.x + kNodeW * m_AnimZoom && s.y <= p.y + kNodeH * m_AnimZoom) return c.states[i].name;
        }
        for (const std::string& special : { std::string(kAny), kEntry })
        {
            const ImVec2 p = toScreen(nodePos(special));
            if (s.x >= p.x && s.y >= p.y && s.x <= p.x + kNodeW * m_AnimZoom && s.y <= p.y + kNodeH * m_AnimZoom) return special;
        }
        return {};
    };

    // Transitions (arrows); pairs in both directions are offset so both stay clickable.
    auto arrow = [&](ImVec2 a, ImVec2 b, ImU32 col, float thickness) {
        dl->AddLine(a, b, col, thickness);
        const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::max(std::sqrt(dx * dx + dy * dy), 1e-3f);
        dx /= len;
        dy /= len;
        const float s = 7.0f * std::max(m_AnimZoom, 0.6f);
        dl->AddTriangleFilled(ImVec2(mid.x + dx * s, mid.y + dy * s), ImVec2(mid.x - dx * s - dy * s * 0.8f, mid.y - dy * s + dx * s * 0.8f),
                              ImVec2(mid.x - dx * s + dy * s * 0.8f, mid.y - dy * s - dx * s * 0.8f), col);
    };
    int hoveredTransition = -1;
    for (size_t i = 0; i < c.transitions.size(); ++i)
    {
        const AnimTransition& t = c.transitions[i];
        if (t.from != kAny && c.FindState(t.from) < 0) continue;
        if (c.FindState(t.to) < 0) continue;
        ImVec2 a = nodeCenter(t.from), b = nodeCenter(t.to);
        bool reverse = false;
        for (const AnimTransition& o : c.transitions) reverse |= o.from == t.to && o.to == t.from;
        if (reverse)
        {
            float dx = b.x - a.x, dy = b.y - a.y;
            const float len = std::max(std::sqrt(dx * dx + dy * dy), 1e-3f);
            const ImVec2 n(-dy / len * 6.0f, dx / len * 6.0f);
            a = ImVec2(a.x + n.x, a.y + n.y);
            b = ImVec2(b.x + n.x, b.y + n.y);
        }
        const bool selected = m_AnimSelTransition == static_cast<int>(i);
        const bool over = hovered && SegmentDistance(mouse, a, b) < 5.0f && nodeAt(mouse).empty();
        if (over) hoveredTransition = static_cast<int>(i);
        const bool active = instance && instance->NextState() >= 0 && instance->Controller()->states[instance->NextState()].name == t.to &&
                            (t.from == kAny || (instance->CurrentState() >= 0 && instance->Controller()->states[instance->CurrentState()].name == t.from));
        arrow(a, b, selected ? Col(88, 160, 255) : active ? Col(80, 180, 255) : over ? Col(230, 230, 230) : Col(200, 200, 200), selected ? 3.0f : 2.0f);
    }
    // Entry -> default state
    if (c.FindState(c.defaultState) >= 0) arrow(nodeCenter(kEntry), nodeCenter(c.defaultState), Col(220, 140, 60), 2.0f);

    // Transition being created
    if (!m_AnimLinkFrom.empty())
    {
        arrow(nodeCenter(m_AnimLinkFrom), mouse, Col(255, 255, 255), 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_AnimLinkFrom.clear();
    }

    // Nodes
    auto drawNode = [&](const std::string& name, const std::string& label, glm::vec2 pos, ImU32 fill, bool selected, float progress,
                        const char* subtitle) {
        const ImVec2 p = toScreen(pos);
        const ImVec2 q(p.x + kNodeW * m_AnimZoom, p.y + kNodeH * m_AnimZoom);
        const float r = 5.0f * m_AnimZoom;
        dl->AddRectFilled(ImVec2(p.x + 2, p.y + 3), ImVec2(q.x + 2, q.y + 3), Col(0, 0, 0, 90), r);
        dl->AddRectFilled(p, q, fill, r);
        if (progress >= 0.0f)
        {
            const float h = 5.0f * m_AnimZoom;
            dl->AddRectFilled(ImVec2(p.x + 4, q.y - h - 3), ImVec2(q.x - 4, q.y - 3), Col(30, 30, 30), 2.0f);
            dl->AddRectFilled(ImVec2(p.x + 4, q.y - h - 3), ImVec2(p.x + 4 + (q.x - p.x - 8) * progress, q.y - 3), Col(70, 160, 255), 2.0f);
        }
        dl->AddRect(p, q, selected ? Col(88, 160, 255) : Col(20, 20, 20), r, 0, selected ? 2.5f : 1.0f);
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * std::clamp(m_AnimZoom, 0.6f, 1.4f);
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label.c_str());
        const float ty = subtitle ? p.y + (q.y - p.y) * 0.5f - fs * 0.9f : p.y + (q.y - p.y - fs) * 0.5f;
        dl->AddText(font, fs, ImVec2(p.x + (q.x - p.x - ts.x) * 0.5f, ty), Col(235, 235, 235), label.c_str());
        if (subtitle)
        {
            const float fs2 = fs * 0.8f;
            const ImVec2 s2 = font->CalcTextSizeA(fs2, FLT_MAX, 0.0f, subtitle);
            dl->AddText(font, fs2, ImVec2(p.x + (q.x - p.x - s2.x) * 0.5f, ty + fs), Col(190, 190, 190), subtitle);
        }
        (void)name;
    };
    drawNode(kEntry, "Entry", c.entryPosition, Col(40, 110, 50), false, -1.0f, nullptr);
    drawNode(kAny, "Any State", c.anyStatePosition, Col(40, 110, 120), m_AnimSelState == kAny, -1.0f, nullptr);
    for (size_t i = 0; i < c.states.size(); ++i)
    {
        const AnimState& s = c.states[i];
        const bool isDefault = s.name == c.defaultState;
        float progress = -1.0f;
        if (instance)
        {
            if (instance->CurrentState() == static_cast<int>(i))
                progress = s.loop ? instance->CurrentNormalizedTime() - std::floor(instance->CurrentNormalizedTime())
                                  : std::min(instance->CurrentNormalizedTime(), 1.0f);
            else if (instance->NextState() == static_cast<int>(i)) progress = instance->TransitionProgress();
        }
        const char* subtitle = s.type == AnimMotionType::BlendTree2D ? "Blend Tree 2D" : s.type == AnimMotionType::BlendTree1D ? "Blend Tree 1D" : nullptr;
        drawNode(s.name, s.name, s.position, isDefault ? Col(194, 100, 26) : Col(72, 72, 72), m_AnimSelState == s.name, progress, subtitle);
    }

    // Interaction
    const std::string under = hovered ? nodeAt(mouse) : std::string();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        if (!m_AnimLinkFrom.empty())
        {
            // Finish "Make Transition" on a state.
            if (!under.empty() && under != kEntry && under != kAny && under != m_AnimLinkFrom)
            {
                AnimTransition t;
                t.from = m_AnimLinkFrom;
                t.to = under;
                t.hasExitTime = m_AnimLinkFrom != kAny;
                c.transitions.push_back(t);
                m_AnimSelTransition = static_cast<int>(c.transitions.size()) - 1;
                m_AnimSelState.clear();
                m_AnimCtrlDirty = true;
            }
            m_AnimLinkFrom.clear();
        }
        else if (!under.empty())
        {
            m_AnimSelState = under == kEntry ? std::string() : under;
            m_AnimSelTransition = -1;
            m_AnimDragNode = under;
        }
        else if (hoveredTransition >= 0)
        {
            m_AnimSelTransition = hoveredTransition;
            m_AnimSelState.clear();
        }
        else
        {
            m_AnimSelState.clear();
            m_AnimSelTransition = -1;
        }
    }
    if (!m_AnimDragNode.empty() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt)
    {
        const glm::vec2 d(io.MouseDelta.x / m_AnimZoom, io.MouseDelta.y / m_AnimZoom);
        if (m_AnimDragNode == kEntry) c.entryPosition += d;
        else if (m_AnimDragNode == kAny) c.anyStatePosition += d;
        else if (int i = c.FindState(m_AnimDragNode); i >= 0) c.states[i].position += d;
        m_AnimCtrlDirty = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_AnimDragNode.clear();

    // Context menus
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        m_AnimContextNode = under;
        m_AnimContextPos = toCanvas(mouse);
        m_AnimLinkFrom.clear();
        ImGui::OpenPopup(under.empty() ? "GraphContext" : "NodeContext");
    }
    auto addState = [&](AnimMotionType type, const std::string& clip, const std::string& base) {
        AnimState s;
        s.type = type;
        s.clip = clip;
        s.name = c.UniqueStateName(base);
        s.position = m_AnimContextPos;
        if (type == AnimMotionType::BlendTree1D || type == AnimMotionType::BlendTree2D)
        {
            for (const AnimParam& p : c.params)
                if (p.type == AnimParamType::Float)
                {
                    if (s.paramX.empty()) s.paramX = p.name;
                    else if (s.paramY.empty()) s.paramY = p.name;
                }
        }
        if (c.states.empty() || c.FindState(c.defaultState) < 0) c.defaultState = s.name;
        c.states.push_back(s);
        m_AnimSelState = s.name;
        m_AnimSelTransition = -1;
        m_AnimCtrlDirty = true;
    };
    if (ImGui::BeginPopup("GraphContext"))
    {
        if (ImGui::BeginMenu("Create State"))
        {
            if (ImGui::MenuItem("Empty")) addState(AnimMotionType::Clip, "", "New State");
            if (ImGui::MenuItem("From New Blend Tree (1D)")) addState(AnimMotionType::BlendTree1D, "", "Blend Tree");
            if (ImGui::MenuItem("From New Blend Tree (2D)")) addState(AnimMotionType::BlendTree2D, "", "Blend Tree");
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("NodeContext"))
    {
        const std::string node = m_AnimContextNode;
        if (node != kEntry && ImGui::MenuItem("Make Transition")) m_AnimLinkFrom = node;
        if (node != kEntry && node != kAny)
        {
            if (ImGui::MenuItem("Set as Layer Default State", nullptr, false, c.defaultState != node))
            {
                c.defaultState = node;
                m_AnimCtrlDirty = true;
            }
            if (ImGui::MenuItem("Copy (Duplicate)"))
            {
                if (int i = c.FindState(node); i >= 0)
                {
                    AnimState copy = c.states[i];
                    copy.name = c.UniqueStateName(node);
                    copy.position += glm::vec2(30.0f, 30.0f);
                    c.states.push_back(copy);
                    m_AnimCtrlDirty = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
            {
                c.RemoveState(node);
                m_AnimSelState.clear();
                m_AnimSelTransition = -1;
                m_AnimCtrlDirty = true;
            }
        }
        ImGui::EndPopup();
    }

    // Drop FBX clips onto the graph to create states.
    if (ImGui::BeginDragDropTarget())
    {
        std::string asset;
        if (AcceptAssetDrop(".fbx", asset) || AcceptAssetDrop(".FBX", asset))
        {
            m_AnimContextPos = toCanvas(mouse);
            addState(AnimMotionType::Clip, asset, fs::path(asset).stem().string());
        }
        ImGui::EndDragDropTarget();
    }

    // Delete key
    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (m_AnimSelTransition >= 0 && m_AnimSelTransition < static_cast<int>(c.transitions.size()))
        {
            c.transitions.erase(c.transitions.begin() + m_AnimSelTransition);
            m_AnimSelTransition = -1;
            m_AnimCtrlDirty = true;
        }
        else if (!m_AnimSelState.empty() && m_AnimSelState != kAny)
        {
            c.RemoveState(m_AnimSelState);
            m_AnimSelState.clear();
            m_AnimCtrlDirty = true;
        }
    }

    if (c.states.empty())
    {
        const char* hint = "Right-click to create a state, or drag an FBX animation here.";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(origin.x + (canvas.x - ts.x) * 0.5f, origin.y + canvas.y * 0.5f), Col(150, 150, 150), hint);
    }
    dl->PopClipRect();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Properties of the selected state / transition
// ---------------------------------------------------------------------------
void Editor::DrawAnimatorSelection(AnimatorController& c, AnimatorInstance* instance)
{
    auto floatParams = [&]() {
        std::vector<std::string> names;
        for (const AnimParam& p : c.params)
            if (p.type == AnimParamType::Float || p.type == AnimParamType::Int) names.push_back(p.name);
        return names;
    };
    auto paramCombo = [&](const char* label, std::string& value) {
        EditorUI::PropertyLabel(label);
        bool changed = false;
        if (ImGui::BeginCombo((std::string("##") + label).c_str(), value.empty() ? "(none)" : value.c_str()))
        {
            for (const std::string& n : floatParams())
                if (ImGui::Selectable(n.c_str(), n == value)) { value = n; changed = true; }
            ImGui::EndCombo();
        }
        return changed;
    };

    // ----- Transition -----
    if (m_AnimSelTransition >= 0 && m_AnimSelTransition < static_cast<int>(c.transitions.size()))
    {
        AnimTransition& t = c.transitions[m_AnimSelTransition];
        ImGui::Text("%s  ->  %s", t.from.c_str(), t.to.c_str());
        ImGui::Separator();
        EditorUI::PropertyLabel("Has Exit Time");
        if (ImGui::Checkbox("##exit", &t.hasExitTime)) m_AnimCtrlDirty = true;
        if (t.hasExitTime)
        {
            EditorUI::PropertyLabel("Exit Time");
            if (ImGui::DragFloat("##exitTime", &t.exitTime, 0.01f, 0.0f, 10.0f, "%.2f")) m_AnimCtrlDirty = true;
        }
        EditorUI::PropertyLabel("Transition Duration (s)");
        if (ImGui::DragFloat("##duration", &t.duration, 0.01f, 0.0f, 5.0f, "%.2f")) m_AnimCtrlDirty = true;

        ImGui::SeparatorText("Conditions");
        int remove = -1;
        for (size_t i = 0; i < t.conditions.size(); ++i)
        {
            AnimCondition& cond = t.conditions[i];
            ImGui::PushID(static_cast<int>(i));
            const int pi = c.FindParam(cond.param);
            const AnimParamType type = pi >= 0 ? c.params[pi].type : AnimParamType::Float;
            ImGui::SetNextItemWidth(110);
            if (ImGui::BeginCombo("##param", cond.param.empty() ? "(param)" : cond.param.c_str()))
            {
                for (const AnimParam& p : c.params)
                    if (ImGui::Selectable(p.name.c_str(), p.name == cond.param))
                    {
                        cond.param = p.name;
                        cond.mode = p.type == AnimParamType::Bool || p.type == AnimParamType::Trigger ? AnimConditionMode::If
                                                                                                     : AnimConditionMode::Greater;
                        m_AnimCtrlDirty = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (type == AnimParamType::Bool)
            {
                ImGui::SetNextItemWidth(80);
                int v = cond.mode == AnimConditionMode::IfNot ? 1 : 0;
                if (ImGui::Combo("##mode", &v, "true\0false\0")) { cond.mode = v ? AnimConditionMode::IfNot : AnimConditionMode::If; m_AnimCtrlDirty = true; }
            }
            else if (type == AnimParamType::Float || type == AnimParamType::Int)
            {
                const char* modes = type == AnimParamType::Float ? "Greater\0Less\0" : "Greater\0Less\0Equals\0NotEqual\0";
                int v = static_cast<int>(cond.mode) - static_cast<int>(AnimConditionMode::Greater);
                ImGui::SetNextItemWidth(80);
                if (ImGui::Combo("##mode", &v, modes)) { cond.mode = static_cast<AnimConditionMode>(v + 2); m_AnimCtrlDirty = true; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                if (ImGui::DragFloat("##threshold", &cond.threshold, 0.01f, 0.0f, 0.0f, type == AnimParamType::Int ? "%.0f" : "%.2f"))
                    m_AnimCtrlDirty = true;
            }
            else
            {
                ImGui::TextDisabled("(trigger)");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) { t.conditions.erase(t.conditions.begin() + remove); m_AnimCtrlDirty = true; }
        if (ImGui::SmallButton("+ Condition"))
        {
            AnimCondition cond;
            if (!c.params.empty())
            {
                cond.param = c.params[0].name;
                if (c.params[0].type == AnimParamType::Bool || c.params[0].type == AnimParamType::Trigger) cond.mode = AnimConditionMode::If;
            }
            t.conditions.push_back(cond);
            m_AnimCtrlDirty = true;
        }
        if (!t.hasExitTime && t.conditions.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "A transition needs an exit time or at least one condition.");
        ImGui::Spacing();
        if (ImGui::Button("Delete Transition"))
        {
            c.transitions.erase(c.transitions.begin() + m_AnimSelTransition);
            m_AnimSelTransition = -1;
            m_AnimCtrlDirty = true;
        }
        return;
    }

    // ----- State -----
    const int si = c.FindState(m_AnimSelState);
    if (si < 0)
    {
        if (m_AnimSelState == kAny) ImGui::TextWrapped("Any State: transitions from here can fire from every state (right-click > Make Transition).");
        else ImGui::TextDisabled("Select a state or transition.");
        ImGui::Spacing();
        ImGui::TextDisabled("Right-click the graph: create states.");
        ImGui::TextDisabled("Right-click a state: Make Transition, default, delete.");
        ImGui::TextDisabled("Drag FBX animations onto the graph.");
        ImGui::TextDisabled("Middle mouse / Alt+drag: pan. Wheel: zoom.");
        return;
    }
    AnimState& s = c.states[si];
    char name[64];
    std::snprintf(name, sizeof(name), "%s", s.name.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##stateName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue) && name[0] && c.FindState(name) < 0 &&
        std::string(name) != kAny)
    {
        c.RenameState(s.name, name);
        m_AnimSelState = name;
        m_AnimCtrlDirty = true;
        return;
    }
    if (s.name == c.defaultState) ImGui::TextColored(ImVec4(0.9f, 0.55f, 0.2f, 1.0f), "Default state");
    ImGui::Separator();

    EditorUI::PropertyLabel("Motion");
    int type = static_cast<int>(s.type);
    if (ImGui::Combo("##type", &type, "Clip\0Blend Tree 1D\0Blend Tree 2D (Freeform)\0")) { s.type = static_cast<AnimMotionType>(type); m_AnimCtrlDirty = true; }
    EditorUI::PropertyLabel("Speed");
    if (ImGui::DragFloat("##speed", &s.speed, 0.01f, -5.0f, 5.0f, "%.2f")) m_AnimCtrlDirty = true;
    EditorUI::PropertyLabel("Loop");
    if (ImGui::Checkbox("##loop", &s.loop)) m_AnimCtrlDirty = true;

    auto clipInfo = [&](const std::string& ref) {
        if (ref.empty()) return;
        if (const AnimationClip* clip = m_Animation.Clips().Get(ref))
        {
            if (clip->hasRootMotion)
                ImGui::TextDisabled("%.2f s, root motion %.2f m/s", clip->duration, clip->averageSpeed);
            else
                ImGui::TextDisabled("%.2f s, in place", clip->duration);
        }
        else ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "No animation in this file.");
    };

    if (s.type == AnimMotionType::Clip)
    {
        EditorUI::PropertyLabel("Clip");
        if (ClipField("clip", s.clip)) m_AnimCtrlDirty = true;
        clipInfo(s.clip);
    }
    else
    {
        const bool twoD = s.type == AnimMotionType::BlendTree2D;
        if (paramCombo(twoD ? "Parameter X" : "Parameter", s.paramX)) m_AnimCtrlDirty = true;
        if (twoD && paramCombo("Parameter Y", s.paramY)) m_AnimCtrlDirty = true;

        ImGui::SeparatorText("Motions");
        int remove = -1;
        for (size_t i = 0; i < s.children.size(); ++i)
        {
            BlendChild& ch = s.children[i];
            ImGui::PushID(static_cast<int>(i));
            if (ClipField("childClip", ch.clip)) m_AnimCtrlDirty = true;
            if (twoD)
            {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                if (ImGui::DragFloat2("##pos", &ch.position.x, 0.01f, 0.0f, 0.0f, "%.2f")) m_AnimCtrlDirty = true;
            }
            else
            {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
                if (ImGui::DragFloat("##threshold", &ch.threshold, 0.01f, 0.0f, 0.0f, "threshold %.2f")) m_AnimCtrlDirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(40);
            if (ImGui::DragFloat("##childSpeed", &ch.speed, 0.01f, 0.05f, 5.0f, "%.1fx")) m_AnimCtrlDirty = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) remove = static_cast<int>(i);
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (remove >= 0) { s.children.erase(s.children.begin() + remove); m_AnimCtrlDirty = true; }
        if (ImGui::SmallButton("+ Motion"))
        {
            BlendChild ch;
            if (!s.children.empty()) ch = s.children.back();
            ch.clip.clear();
            s.children.push_back(ch);
            m_AnimCtrlDirty = true;
        }

        // 2D blend space diagram: motions as dots, the current parameter value as a red marker.
        if (twoD && !s.children.empty())
        {
            ImGui::Spacing();
            const float size = std::min(ImGui::GetContentRegionAvail().x, 240.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(size, size));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            glm::vec2 mn(-1.0f), mx(1.0f);
            for (const BlendChild& ch : s.children) { mn = glm::min(mn, ch.position); mx = glm::max(mx, ch.position); }
            const glm::vec2 ext = glm::max(glm::abs(mn), glm::abs(mx)) * 1.15f;
            auto map = [&](glm::vec2 v) { return ImVec2(p.x + size * (0.5f + 0.5f * v.x / ext.x), p.y + size * (0.5f - 0.5f * v.y / ext.y)); };
            dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), Col(30, 30, 30), 4.0f);
            dl->AddLine(map({ -ext.x, 0 }), map({ ext.x, 0 }), Col(60, 60, 60));
            dl->AddLine(map({ 0, -ext.y }), map({ 0, ext.y }), Col(60, 60, 60));
            for (const BlendChild& ch : s.children) dl->AddCircleFilled(map(ch.position), 4.0f, Col(90, 160, 255));
            const float px = instance ? instance->GetParam(s.paramX) : 0.0f, py = instance ? instance->GetParam(s.paramY) : 0.0f;
            dl->AddCircleFilled(map({ px, py }), 5.0f, Col(230, 70, 60));
            ImGui::TextDisabled("X: %s   Y: %s", s.paramX.c_str(), s.paramY.c_str());
        }
    }

    // Transitions from this state
    ImGui::SeparatorText("Transitions");
    bool any = false;
    for (size_t i = 0; i < c.transitions.size(); ++i)
    {
        const AnimTransition& t = c.transitions[i];
        if (t.from != s.name) continue;
        any = true;
        const std::string label = s.name + " -> " + t.to + "##" + std::to_string(i);
        if (ImGui::Selectable(label.c_str())) { m_AnimSelTransition = static_cast<int>(i); }
    }
    if (!any) ImGui::TextDisabled("None (right-click the state > Make Transition)");
}
