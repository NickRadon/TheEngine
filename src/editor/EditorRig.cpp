#include "editor/Editor.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{
struct RigLine { std::string text, command; std::vector<std::string> args; };
struct RigGraph
{
    std::string path;
    std::vector<RigLine> lines;
    int selected = -1;
    ImVec2 pan = ImVec2(0, 0);
};
RigGraph g;

std::vector<std::string> Tokens(const std::string& line)
{
    std::vector<std::string> result;
    std::istringstream stream(line);
    std::string token;
    while (stream >> std::ws && stream.peek() != EOF)
    {
        if (stream.peek() == '"') stream >> std::quoted(token);
        else stream >> token;
        if (!stream.fail()) result.push_back(token);
    }
    return result;
}
std::string Quote(const std::string& value) { std::ostringstream out; out << std::quoted(value); return out.str(); }
bool IsNode(const RigLine& line)
{
    static const char* commands[] = { "bone", "precopy", "prerotate", "copy", "move", "rotate", "addlocalrot", "modify", "twobone" };
    for (const char* command : commands) if (line.command == command) return true;
    return false;
}
int Stage(const RigLine& line)
{
    if (line.command == "bone") return 0;
    if (line.command == "precopy" || line.command == "prerotate") return 1;
    return 2;
}
void ParseGraph(const std::string& text, const std::string& path)
{
    g = RigGraph{};
    g.path = path;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        RigLine entry;
        entry.text = line;
        auto tokens = Tokens(line);
        if (!tokens.empty()) { entry.command = tokens.front(); entry.args.assign(tokens.begin() + 1, tokens.end()); }
        g.lines.push_back(std::move(entry));
    }
}
std::string SerializeGraph()
{
    std::string text;
    for (const RigLine& line : g.lines) text += line.text + "\n";
    return text;
}
void Rebuild(RigLine& line)
{
    line.text = line.command;
    for (size_t i = 0; i < line.args.size(); ++i)
    {
        const bool name = i < (line.command == "twobone" ? 3u : 2u);
        line.text += " " + (name ? Quote(line.args[i]) : line.args[i]);
    }
}
void AddNode(const char* command, const char* rest)
{
    RigLine line;
    line.text = std::string(command) + " " + rest;
    auto tokens = Tokens(line.text);
    line.command = tokens.front();
    line.args.assign(tokens.begin() + 1, tokens.end());
    const int stage = Stage(line);
    int at = static_cast<int>(g.lines.size());
    for (int i = 0; i < static_cast<int>(g.lines.size()); ++i)
        if (IsNode(g.lines[i]) && Stage(g.lines[i]) > stage) { at = i; break; }
    g.lines.insert(g.lines.begin() + at, std::move(line));
    g.selected = at;
}
ImU32 StageColor(int stage) { return stage == 0 ? IM_COL32(86, 167, 190, 255) : stage == 1 ? IM_COL32(204, 157, 83, 255) : IM_COL32(143, 184, 107, 255); }
const char* FieldLabel(const RigLine& line, int index)
{
    const std::string& op = line.command;
    if (index == 0) return op == "bone" ? "Helper bone name" : op == "twobone" ? "End bone (hand)" : "Target bone";
    if (index == 1) return op == "bone" ? "Parent bone" : op == "twobone" ? "IK target bone" :
                           (op == "precopy" || op == "copy") ? "Source bone" : "Space bone";
    if (op == "twobone") return index == 2 ? "Pole / hint bone (optional)" : "IK weight";
    if (op == "precopy" || op == "copy")
    {
        static const char* labels[] = { "Blend weight", "Copy position", "Copy rotation", "Copy scale" };
        return index >= 2 && index <= 5 ? labels[index - 2] : "Parameter";
    }
    if (op == "bone" || op == "move" || op == "modify")
    {
        static const char* position[] = { "Position X (m)", "Position Y (m)", "Position Z (m)" };
        if (index >= 2 && index <= 4) return position[index - 2];
    }
    if (op == "modify" && index == 9) return "Blend weight";
    const int rotationStart = op == "bone" || op == "modify" ? 5 : 2;
    static const char* rotation[] = { "Rotation X", "Rotation Y", "Rotation Z", "Rotation W" };
    if (index >= rotationStart && index < rotationStart + 4) return rotation[index - rotationStart];
    return "Parameter";
}
bool IsCopyFlag(const RigLine& line, int index)
{ return (line.command == "copy" || line.command == "precopy") && index >= 3 && index <= 5; }
}

void Editor::OpenRig(const std::string& path)
{
    std::ifstream in(path);
    const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && contents.empty()) { Notify("Could not open rig " + fs::path(path).filename().string()); return; }
    if (contents.rfind("TheEngineRig 1", 0) != 0 && contents.rfind("TheEngineRig 2", 0) != 0)
    { Notify("Not a TheEngine rig: " + fs::path(path).filename().string()); return; }
    if (contents.size() >= sizeof(m_RigText)) { Notify("Rig is too large for the editor"); return; }
    std::snprintf(m_RigText, sizeof(m_RigText), "%s", contents.c_str());
    m_RigPath = path;
    ParseGraph(contents, path);
    m_RigDirty = false;
    m_FocusRig = true;
}

void Editor::SaveRig()
{
    if (m_RigPath.empty()) return;
    const std::string contents = SerializeGraph();
    if (contents.size() >= sizeof(m_RigText)) { Notify("Rig is too large for the editor"); return; }
    std::ofstream out(m_RigPath, std::ios::trunc);
    out << contents;
    if (!out) { Notify("Could not save rig " + fs::path(m_RigPath).filename().string()); return; }
    std::snprintf(m_RigText, sizeof(m_RigText), "%s", contents.c_str());
    m_RigDirty = false;
}

void Editor::DrawRigWindow()
{
    if (m_RigPath.empty()) return;
    if (g.path != m_RigPath) ParseGraph(m_RigText, m_RigPath);
    if (m_FocusRig) { ImGui::SetNextWindowFocus(); m_FocusRig = false; }
    ImGui::SetNextWindowSize(ImVec2(1120, 720), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Rig Controls")) { ImGui::End(); return; }
    ImGui::Text("%s", fs::path(m_RigPath).filename().string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Save")) SaveRig();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) OpenRig(m_RigPath);
    ImGui::SameLine();
    if (m_RigDirty) ImGui::TextColored(ImVec4(1, .7f, .25f, 1), "Unsaved changes");
    ImGui::TextDisabled("Execution flows left to right. Select a node to edit; drag the canvas to pan.");
    if (ImGui::BeginCombo("Add node", "Choose control..."))
    {
        struct Choice { const char* title; const char* command; const char* args; };
        const Choice choices[] = {
            {"Helper Bone", "bone", "\"helper\" \"root\" 0 0 0 0 0 0 1"},
            {"Copy Before Look", "precopy", "\"helper\" \"source\" 1 1 1 1"},
            {"Rotate Before Look", "prerotate", "\"bone\" \"space\" 0 0 0 1"},
            {"Copy Bone", "copy", "\"helper\" \"source\" 1 1 1 1"},
            {"Modify Bone", "modify", "\"bone\" \"space\" 0 0 0 0 0 0 1 1"},
            {"Two Bone IK", "twobone", "\"hand_l\" \"target\" \"\" 1"}
        };
        for (const Choice& choice : choices) if (ImGui::Selectable(choice.title)) { AddNode(choice.command, choice.args); m_RigDirty = true; }
        ImGui::EndCombo();
    }
    const float inspectorWidth = 305.0f;
    const float canvasWidth = std::max(320.0f, ImGui::GetContentRegionAvail().x - inspectorWidth - 10.0f);
    ImGui::BeginChild("Rig graph", ImVec2(canvasWidth, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(29, 34, 42, 255));
    // A canvas-sized item would overlap every node and win ImGui's hit test.
    // Use the child window for background gestures; nodes below own their hit boxes.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    { g.pan.x += ImGui::GetIO().MouseDelta.x; g.pan.y += ImGui::GetIO().MouseDelta.y; }
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for (float x = origin.x + std::fmod(g.pan.x, 32.0f); x < origin.x + size.x; x += 32.0f)
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), IM_COL32(44, 50, 60, 255));
    for (float y = origin.y + std::fmod(g.pan.y, 32.0f); y < origin.y + size.y; y += 32.0f)
        draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), IM_COL32(44, 50, 60, 255));
    const char* headings[] = { "HELPER BONES", "BEFORE SPINE LOOK", "AFTER SPINE LOOK" };
    const float columns[] = { 24.0f, 265.0f, 506.0f };
    int counts[3] = {};
    ImVec2 previous[3] = {};
    ImVec2 first[3] = {}, last[3] = {};
    int row[3] = {};
    for (const RigLine& line : g.lines)
    {
        if (!IsNode(line)) continue;
        const int stage = Stage(line);
        const ImVec2 pos(origin.x + columns[stage] + g.pan.x, origin.y + 90 + g.pan.y + row[stage]++ * 100.0f);
        if (first[stage].x == 0) first[stage] = pos;
        last[stage] = ImVec2(pos.x + 212, pos.y);
    }
    for (int stage = 0; stage < 2; ++stage)
    {
        if (last[stage].x == 0 || first[stage + 1].x == 0) continue;
        const ImVec2 from = last[stage], to = first[stage + 1];
        draw->AddBezierCubic(from, ImVec2(from.x + 40, from.y), ImVec2(to.x - 40, to.y), to,
                             IM_COL32(190, 198, 210, 220), 2.5f);
        if (stage == 1) draw->AddText(ImVec2(from.x + 4, (from.y + to.y) * .5f - 18),
                                      IM_COL32(220, 192, 125, 255), "LOOK");
    }
    for (int stage = 0; stage < 3; ++stage)
    {
        const ImVec2 pos(origin.x + columns[stage] + g.pan.x, origin.y + 18 + g.pan.y);
        draw->AddText(pos, StageColor(stage), headings[stage]);
    }
    for (int i = 0; i < static_cast<int>(g.lines.size()); ++i)
    {
        const RigLine& line = g.lines[i];
        if (!IsNode(line)) continue;
        const int stage = Stage(line);
        const ImVec2 pos(origin.x + columns[stage] + g.pan.x, origin.y + 54 + g.pan.y + counts[stage]++ * 100.0f);
        const ImVec2 end(pos.x + 212, pos.y + 72);
        if (previous[stage].x != 0)
        {
            const ImVec2 from(previous[stage].x, previous[stage].y);
            const ImVec2 to(pos.x, pos.y + 36);
            draw->AddBezierCubic(from, ImVec2(from.x + 45, from.y), ImVec2(to.x - 45, to.y), to, StageColor(stage), 2.0f);
        }
        previous[stage] = ImVec2(end.x, pos.y + 36);
        draw->AddRectFilled(pos, end, IM_COL32(50, 58, 71, 255), 6);
        draw->AddRect(pos, end, i == g.selected ? IM_COL32(255, 221, 116, 255) : StageColor(stage), 6, 0, i == g.selected ? 3.0f : 1.5f);
        draw->AddRectFilled(pos, ImVec2(end.x, pos.y + 6), StageColor(stage), 4);
        draw->AddCircleFilled(ImVec2(pos.x, pos.y + 36), 5, StageColor(stage));
        draw->AddCircleFilled(ImVec2(end.x, pos.y + 36), 5, StageColor(stage));
        draw->AddText(ImVec2(pos.x + 12, pos.y + 15), IM_COL32(240, 244, 249, 255), line.command.c_str());
        const std::string summary = line.args.empty() ? "" : line.args[0] + (line.args.size() > 1 ? "  <-  " + line.args[1] : "");
        draw->AddText(ImVec2(pos.x + 12, pos.y + 43), IM_COL32(178, 192, 208, 255), summary.c_str());
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(i);
        ImGui::InvisibleButton("node", ImVec2(212, 72));
        if (ImGui::IsItemClicked()) g.selected = i;
        ImGui::PopID();
    }
    draw->PopClipRect();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Node inspector", ImVec2(0, 0), true);
    ImGui::SeparatorText("Node inspector");
    if (g.selected >= 0 && g.selected < static_cast<int>(g.lines.size()) && IsNode(g.lines[g.selected]))
    {
        RigLine& line = g.lines[g.selected];
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(StageColor(Stage(line))), "%s", line.command.c_str());
        ImGui::TextDisabled("Edit this control's inputs. Changes take effect after Save.");
        for (int i = 0; i < static_cast<int>(line.args.size()); ++i)
        {
            ImGui::PushID(i);
            if (i == 2) ImGui::SeparatorText(line.command == "twobone" ? "IK settings" : "Transform / settings");
            if (IsCopyFlag(line, i))
            {
                bool enabled = line.args[i] != "0";
                if (ImGui::Checkbox(FieldLabel(line, i), &enabled))
                { line.args[i] = enabled ? "1" : "0"; Rebuild(line); m_RigDirty = true; }
            }
            else
            {
                ImGui::TextUnformatted(FieldLabel(line, i));
                char value[256];
                std::snprintf(value, sizeof(value), "%s", line.args[i].c_str());
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##value", value, sizeof(value)))
                { line.args[i] = value; Rebuild(line); m_RigDirty = true; }
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Move up"))
        {
            for (int i = g.selected - 1; i >= 0; --i) if (IsNode(g.lines[i]) && Stage(g.lines[i]) == Stage(line))
            { std::swap(g.lines[i], g.lines[g.selected]); g.selected = i; m_RigDirty = true; break; }
        }
        ImGui::SameLine();
        if (ImGui::Button("Move down"))
        {
            for (int i = g.selected + 1; i < static_cast<int>(g.lines.size()); ++i) if (IsNode(g.lines[i]) && Stage(g.lines[i]) == Stage(line))
            { std::swap(g.lines[i], g.lines[g.selected]); g.selected = i; m_RigDirty = true; break; }
        }
        if (ImGui::Button("Delete node")) { g.lines.erase(g.lines.begin() + g.selected); g.selected = -1; m_RigDirty = true; }
        if (g.selected >= 0)
        {
            ImGui::SeparatorText("Serialized control");
            ImGui::TextWrapped("%s", g.lines[g.selected].text.c_str());
        }
    }
    else ImGui::TextDisabled("Select a graph node to edit its bones and values.");
    ImGui::EndChild();
    ImGui::End();
}
