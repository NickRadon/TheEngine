#include "editor/EditorUI.h"

#include <imgui_internal.h>
#include <im_anim.h>

#include <cmath>
#include <filesystem>

namespace EditorUI
{
    namespace
    {
        ImFont* g_BoldFont = nullptr;
        ImVec4 g_BaseColors[ImGuiCol_COUNT];

        ImVec4 Hex(unsigned hex, float a = 1.0f)
        {
            return ImVec4(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a);
        }

        ImVec2 P(ImVec2 c, float s, float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); }

        void Arrow(ImDrawList* dl, ImVec2 from, ImVec2 to, float head, ImU32 col, float thick)
        {
            ImVec2 d = ImVec2(to.x - from.x, to.y - from.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1e-3f) return;
            d = ImVec2(d.x / len, d.y / len);
            ImVec2 n(-d.y, d.x);
            ImVec2 base(to.x - d.x * head, to.y - d.y * head);
            dl->AddLine(from, base, col, thick);
            dl->AddTriangleFilled(to, ImVec2(base.x + n.x * head * 0.6f, base.y + n.y * head * 0.6f),
                                  ImVec2(base.x - n.x * head * 0.6f, base.y - n.y * head * 0.6f), col);
        }
    }

    ImFont* BoldFont() { return g_BoldFont; }

    ImU32 AxisColor(int axis, float alpha)
    {
        static const ImVec4 colors[3] = { Hex(0xDB3B21), Hex(0x6DBE2E), Hex(0x2F6FDB) };
        ImVec4 c = colors[axis % 3];
        c.w = alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    void ApplyUnityTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* c = style.Colors;

        c[ImGuiCol_Text] = Hex(0xD2D2D2);
        c[ImGuiCol_TextDisabled] = Hex(0x858585);
        c[ImGuiCol_WindowBg] = Hex(0x383838);
        c[ImGuiCol_ChildBg] = Hex(0x383838, 0.0f);
        c[ImGuiCol_PopupBg] = Hex(0x2B2B2B);
        c[ImGuiCol_Border] = Hex(0x191919);
        c[ImGuiCol_BorderShadow] = Hex(0x000000, 0.0f);
        c[ImGuiCol_FrameBg] = Hex(0x2A2A2A);
        c[ImGuiCol_FrameBgHovered] = Hex(0x323232);
        c[ImGuiCol_FrameBgActive] = Hex(0x1E1E1E);
        c[ImGuiCol_TitleBg] = Hex(0x191919);
        c[ImGuiCol_TitleBgActive] = Hex(0x191919);
        c[ImGuiCol_TitleBgCollapsed] = Hex(0x191919);
        c[ImGuiCol_MenuBarBg] = Hex(0x282828);
        c[ImGuiCol_ScrollbarBg] = Hex(0x2E2E2E);
        c[ImGuiCol_ScrollbarGrab] = Hex(0x5E5E5E);
        c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x6E6E6E);
        c[ImGuiCol_ScrollbarGrabActive] = Hex(0x7E7E7E);
        c[ImGuiCol_CheckMark] = Hex(0xD2D2D2);
        c[ImGuiCol_SliderGrab] = Hex(0x9A9A9A);
        c[ImGuiCol_SliderGrabActive] = Hex(0xBDBDBD);
        c[ImGuiCol_Button] = Hex(0x585858);
        c[ImGuiCol_ButtonHovered] = Hex(0x676767);
        c[ImGuiCol_ButtonActive] = Hex(0x46607C);
        c[ImGuiCol_Header] = Hex(0x2C5D87);
        c[ImGuiCol_HeaderHovered] = Hex(0x454545);
        c[ImGuiCol_HeaderActive] = Hex(0x2C5D87);
        c[ImGuiCol_Separator] = Hex(0x232323);
        c[ImGuiCol_SeparatorHovered] = Hex(0x3A79BB);
        c[ImGuiCol_SeparatorActive] = Hex(0x3A79BB);
        c[ImGuiCol_ResizeGrip] = Hex(0x000000, 0.0f);
        c[ImGuiCol_ResizeGripHovered] = Hex(0x3A79BB, 0.6f);
        c[ImGuiCol_ResizeGripActive] = Hex(0x3A79BB);
        c[ImGuiCol_InputTextCursor] = Hex(0xD2D2D2);
        c[ImGuiCol_Tab] = Hex(0x282828);
        c[ImGuiCol_TabHovered] = Hex(0x444444);
        c[ImGuiCol_TabSelected] = Hex(0x3C3C3C);
        c[ImGuiCol_TabSelectedOverline] = Hex(0x3A79BB);
        c[ImGuiCol_TabDimmed] = Hex(0x282828);
        c[ImGuiCol_TabDimmedSelected] = Hex(0x383838);
        c[ImGuiCol_TabDimmedSelectedOverline] = Hex(0x383838, 0.0f);
        c[ImGuiCol_DockingPreview] = Hex(0x3A79BB, 0.5f);
        c[ImGuiCol_DockingEmptyBg] = Hex(0x191919);
        c[ImGuiCol_PlotLines] = Hex(0x9A9A9A);
        c[ImGuiCol_PlotHistogram] = Hex(0x3A79BB);
        c[ImGuiCol_TableHeaderBg] = Hex(0x2E2E2E);
        c[ImGuiCol_TableBorderStrong] = Hex(0x191919);
        c[ImGuiCol_TableBorderLight] = Hex(0x2A2A2A);
        c[ImGuiCol_TableRowBgAlt] = Hex(0xFFFFFF, 0.02f);
        c[ImGuiCol_TextSelectedBg] = Hex(0x3A79BB, 0.5f);
        c[ImGuiCol_DragDropTarget] = Hex(0x3A79BB);
        c[ImGuiCol_NavCursor] = Hex(0x3A79BB);
        c[ImGuiCol_ModalWindowDimBg] = Hex(0x000000, 0.45f);

        style.WindowPadding = ImVec2(6, 6);
        style.FramePadding = ImVec2(6, 3);
        style.ItemSpacing = ImVec2(6, 4);
        style.ItemInnerSpacing = ImVec2(4, 4);
        style.IndentSpacing = 14.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 9.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = 3.0f;
        style.ChildRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 3.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.DockingSeparatorSize = 2.0f;

        for (int i = 0; i < ImGuiCol_COUNT; ++i) g_BaseColors[i] = c[i];
    }

    void ApplyPlayModeTint(float t)
    {
        static const ImGuiCol tinted[] = {
            ImGuiCol_WindowBg, ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_Tab,
            ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected, ImGuiCol_FrameBg,
            ImGuiCol_DockingEmptyBg, ImGuiCol_PopupBg,
        };
        const ImVec4 tint = Hex(0x1F2A36);
        ImVec4* c = ImGui::GetStyle().Colors;
        for (ImGuiCol idx : tinted)
        {
            const ImVec4& b = g_BaseColors[idx];
            c[idx] = ImVec4(b.x + (tint.x - b.x) * t * 0.6f, b.y + (tint.y - b.y) * t * 0.6f,
                            b.z + (tint.z - b.z) * t * 0.6f, b.w);
        }
    }

    void LoadFonts(float dpiScale)
    {
        ImGuiIO& io = ImGui::GetIO();
        const float size = 15.0f * dpiScale;
        const char* regular = "C:/Windows/Fonts/segoeui.ttf";
        const char* bold = "C:/Windows/Fonts/segoeuib.ttf";
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        if (std::filesystem::exists(regular))
            io.Fonts->AddFontFromFileTTF(regular, size, &cfg);
        else
            io.Fonts->AddFontDefault();
        if (std::filesystem::exists(bold))
            g_BoldFont = io.Fonts->AddFontFromFileTTF(bold, size, &cfg);
        else
            g_BoldFont = io.Fonts->Fonts[0];
    }

    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col)
    {
        const float s = size * 0.5f;
        const float t = std::max(1.0f, size / 12.0f); // line thickness
        switch (icon)
        {
        case Icon::Hand:
        {
            // palm + four fingers + thumb
            dl->AddRectFilled(P(c, s, -0.45f, -0.05f), P(c, s, 0.45f, 0.75f), col, s * 0.25f);
            for (int i = 0; i < 4; ++i)
            {
                float x = -0.45f + i * 0.24f;
                float top = i == 1 || i == 2 ? -0.8f : -0.6f;
                dl->AddRectFilled(P(c, s, x, top), P(c, s, x + 0.18f, 0.1f), col, s * 0.09f);
            }
            dl->AddLine(P(c, s, -0.4f, 0.35f), P(c, s, -0.8f, 0.0f), col, s * 0.2f);
            break;
        }
        case Icon::Move:
        {
            const float head = s * 0.35f;
            Arrow(dl, c, P(c, s, 0, -0.9f), head, col, t);
            Arrow(dl, c, P(c, s, 0, 0.9f), head, col, t);
            Arrow(dl, c, P(c, s, -0.9f, 0), head, col, t);
            Arrow(dl, c, P(c, s, 0.9f, 0), head, col, t);
            break;
        }
        case Icon::Rotate:
        {
            dl->PathArcTo(c, s * 0.65f, IM_PI * 0.15f, IM_PI * 1.75f, 24);
            dl->PathStroke(col, 0, t * 1.2f);
            ImVec2 end(c.x + std::cos(IM_PI * 1.75f) * s * 0.65f, c.y + std::sin(IM_PI * 1.75f) * s * 0.65f);
            dl->AddTriangleFilled(ImVec2(end.x + s * 0.35f, end.y), ImVec2(end.x - s * 0.1f, end.y - s * 0.35f),
                                  ImVec2(end.x - s * 0.1f, end.y + s * 0.3f), col);
            break;
        }
        case Icon::Scale:
        {
            dl->AddRect(P(c, s, -0.8f, -0.8f), P(c, s, 0.8f, 0.8f), col, 0, 0, t);
            dl->AddRectFilled(P(c, s, -0.8f, 0.1f), P(c, s, -0.1f, 0.8f), col);
            Arrow(dl, P(c, s, -0.2f, 0.2f), P(c, s, 0.6f, -0.6f), s * 0.35f, col, t);
            break;
        }
        case Icon::Rect:
        {
            dl->AddRect(P(c, s, -0.75f, -0.6f), P(c, s, 0.75f, 0.6f), col, 0, 0, t);
            const float d = s * 0.16f;
            for (ImVec2 corner : { P(c, s, -0.75f, -0.6f), P(c, s, 0.75f, -0.6f), P(c, s, -0.75f, 0.6f), P(c, s, 0.75f, 0.6f) })
                dl->AddRectFilled(ImVec2(corner.x - d, corner.y - d), ImVec2(corner.x + d, corner.y + d), col);
            dl->AddCircleFilled(c, d, col);
            break;
        }
        case Icon::Transform:
        {
            dl->AddCircle(c, s * 0.62f, col, 24, t);
            const float head = s * 0.3f;
            Arrow(dl, c, P(c, s, 0, -0.95f), head, col, t);
            Arrow(dl, c, P(c, s, 0.95f, 0), head, col, t);
            dl->AddRectFilled(P(c, s, -0.95f, 0.55f), P(c, s, -0.55f, 0.95f), col);
            break;
        }
        case Icon::Play:
            dl->AddTriangleFilled(P(c, s, -0.45f, -0.65f), P(c, s, 0.65f, 0.0f), P(c, s, -0.45f, 0.65f), col);
            break;
        case Icon::Pause:
            dl->AddRectFilled(P(c, s, -0.55f, -0.6f), P(c, s, -0.15f, 0.6f), col);
            dl->AddRectFilled(P(c, s, 0.15f, -0.6f), P(c, s, 0.55f, 0.6f), col);
            break;
        case Icon::Step:
            dl->AddTriangleFilled(P(c, s, -0.6f, -0.6f), P(c, s, 0.3f, 0.0f), P(c, s, -0.6f, 0.6f), col);
            dl->AddRectFilled(P(c, s, 0.35f, -0.6f), P(c, s, 0.65f, 0.6f), col);
            break;
        case Icon::Sun:
        {
            dl->AddCircleFilled(c, s * 0.38f, col, 20);
            for (int i = 0; i < 8; ++i)
            {
                float a = i * IM_PI / 4.0f;
                ImVec2 d(std::cos(a), std::sin(a));
                dl->AddLine(ImVec2(c.x + d.x * s * 0.58f, c.y + d.y * s * 0.58f), ImVec2(c.x + d.x * s * 0.92f, c.y + d.y * s * 0.92f), col, t);
            }
            break;
        }
        case Icon::Camera:
            dl->AddRectFilled(P(c, s, -0.9f, -0.45f), P(c, s, 0.3f, 0.5f), col, s * 0.12f);
            dl->AddTriangleFilled(P(c, s, 0.3f, 0.02f), P(c, s, 0.9f, -0.4f), P(c, s, 0.9f, 0.45f), col);
            dl->AddCircleFilled(P(c, s, -0.55f, -0.65f), s * 0.22f, col);
            dl->AddCircleFilled(P(c, s, -0.05f, -0.65f), s * 0.22f, col);
            break;
        case Icon::Cube:
        case Icon::Empty:
        case Icon::Scene:
        {
            ImVec2 top = P(c, s, 0, -0.85f), l = P(c, s, -0.75f, -0.45f), r = P(c, s, 0.75f, -0.45f);
            ImVec2 mid = P(c, s, 0, -0.05f), bl = P(c, s, -0.75f, 0.45f), br = P(c, s, 0.75f, 0.45f), bot = P(c, s, 0, 0.85f);
            if (icon == Icon::Empty)
            {
                ImVec2 pts[6] = { top, r, br, bot, bl, l };
                dl->AddPolyline(pts, 6, col, ImDrawFlags_Closed, t);
                dl->AddLine(l, mid, col, t); dl->AddLine(r, mid, col, t); dl->AddLine(mid, bot, col, t);
            }
            else
            {
                ImU32 dark = (col & 0x00FFFFFF) | (static_cast<ImU32>(((col >> 24) & 0xFF) * 0.55f) << 24);
                dl->AddQuadFilled(top, r, mid, l, col);
                dl->AddQuadFilled(l, mid, bot, bl, dark);
                dl->AddQuadFilled(mid, r, br, bot, (col & 0x00FFFFFF) | (static_cast<ImU32>(((col >> 24) & 0xFF) * 0.8f) << 24));
            }
            break;
        }
        case Icon::Folder:
            dl->AddRectFilled(P(c, s, -0.9f, -0.65f), P(c, s, -0.1f, -0.3f), col, s * 0.1f);
            dl->AddRectFilled(P(c, s, -0.9f, -0.45f), P(c, s, 0.9f, 0.7f), col, s * 0.1f);
            break;
        case Icon::File:
        {
            ImVec2 pts[5] = { P(c, s, -0.6f, -0.85f), P(c, s, 0.25f, -0.85f), P(c, s, 0.6f, -0.5f), P(c, s, 0.6f, 0.85f), P(c, s, -0.6f, 0.85f) };
            dl->AddConvexPolyFilled(pts, 5, col);
            break;
        }
        case Icon::Grid:
            for (int i = -1; i <= 1; i += 2)
            {
                dl->AddLine(P(c, s, i * 0.33f, -0.85f), P(c, s, i * 0.33f, 0.85f), col, t);
                dl->AddLine(P(c, s, -0.85f, i * 0.33f), P(c, s, 0.85f, i * 0.33f), col, t);
            }
            dl->AddRect(P(c, s, -0.85f, -0.85f), P(c, s, 0.85f, 0.85f), col, 0, 0, t);
            break;
        case Icon::Gizmos:
            dl->AddCircle(c, s * 0.8f, col, 24, t);
            dl->AddEllipse(c, ImVec2(s * 0.8f, s * 0.3f), col, 0.0f, 24, t);
            dl->AddLine(P(c, s, 0, -0.8f), P(c, s, 0, 0.8f), col, t);
            break;
        case Icon::Info:
            dl->AddCircleFilled(c, s * 0.85f, col, 20);
            dl->AddRectFilled(P(c, s, -0.1f, -0.15f), P(c, s, 0.1f, 0.55f), IM_COL32(40, 40, 40, 255));
            dl->AddCircleFilled(P(c, s, 0, -0.45f), s * 0.12f, IM_COL32(40, 40, 40, 255));
            break;
        case Icon::Warning:
            dl->AddTriangleFilled(P(c, s, 0, -0.9f), P(c, s, 0.95f, 0.8f), P(c, s, -0.95f, 0.8f), col);
            dl->AddRectFilled(P(c, s, -0.09f, -0.35f), P(c, s, 0.09f, 0.3f), IM_COL32(40, 40, 40, 255));
            dl->AddCircleFilled(P(c, s, 0, 0.52f), s * 0.1f, IM_COL32(40, 40, 40, 255));
            break;
        case Icon::Error:
            dl->AddCircleFilled(c, s * 0.85f, col, 20);
            dl->AddLine(P(c, s, -0.35f, -0.35f), P(c, s, 0.35f, 0.35f), IM_COL32(40, 40, 40, 255), t * 1.6f);
            dl->AddLine(P(c, s, 0.35f, -0.35f), P(c, s, -0.35f, 0.35f), IM_COL32(40, 40, 40, 255), t * 1.6f);
            break;
        case Icon::Search:
            dl->AddCircle(P(c, s, -0.15f, -0.15f), s * 0.5f, col, 16, t);
            dl->AddLine(P(c, s, 0.22f, 0.22f), P(c, s, 0.8f, 0.8f), col, t * 1.5f);
            break;
        case Icon::Plus:
            dl->AddLine(P(c, s, -0.7f, 0), P(c, s, 0.7f, 0), col, t * 1.4f);
            dl->AddLine(P(c, s, 0, -0.7f), P(c, s, 0, 0.7f), col, t * 1.4f);
            break;
        case Icon::Eye:
            dl->AddEllipse(c, ImVec2(s * 0.9f, s * 0.5f), col, 0.0f, 24, t);
            dl->AddCircleFilled(c, s * 0.28f, col);
            break;
        case Icon::Menu:
            for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(P(c, s, 0, i * 0.5f), s * 0.13f, col);
            break;
        case Icon::Sky:
            dl->PathArcTo(P(c, s, 0, 0.35f), s * 0.5f, IM_PI, IM_PI * 2.0f, 16);
            dl->PathFillConvex(col);
            dl->AddLine(P(c, s, -0.95f, 0.45f), P(c, s, 0.95f, 0.45f), col, t);
            for (int i = 0; i < 5; ++i)
            {
                float a = IM_PI + (i + 0.5f) * IM_PI / 5.0f;
                ImVec2 d(std::cos(a), std::sin(a));
                ImVec2 o = P(c, s, 0, 0.35f);
                dl->AddLine(ImVec2(o.x + d.x * s * 0.65f, o.y + d.y * s * 0.65f), ImVec2(o.x + d.x * s * 0.9f, o.y + d.y * s * 0.9f), col, t);
            }
            break;
        }
    }

    bool IconButton(const char* id, Icon icon, bool selected, const char* tooltip, ImVec2 size)
    {
        const float h = ImGui::GetFrameHeight();
        if (size.x <= 0.0f) size.x = h;
        if (size.y <= 0.0f) size.y = h;
        ImGuiID gid = ImGui::GetID(id);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        ImVec4 target = selected ? ImVec4(0.27f, 0.38f, 0.52f, 1.0f)
                      : held     ? ImVec4(0.2f, 0.2f, 0.2f, 1.0f)
                      : hovered  ? ImVec4(0.33f, 0.33f, 0.33f, 1.0f)
                                 : ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
        ImVec4 bg = iam_tween_color(gid, ImHashStr("bg"), target, 0.12f, iam_ease_preset(iam_ease_out_quad),
                                    iam_policy_crossfade, iam_col_oklab, ImGui::GetIO().DeltaTime, target);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 max(pos.x + size.x, pos.y + size.y);
        dl->AddRectFilled(pos, max, ImGui::ColorConvertFloat4ToU32(bg), 3.0f);
        dl->AddRect(pos, max, IM_COL32(25, 25, 25, 255), 3.0f);
        ImU32 iconCol = selected ? IM_COL32(128, 203, 255, 255) : IM_COL32(210, 210, 210, 255);
        DrawIcon(dl, icon, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f), std::min(size.x, size.y) * 0.62f, iconCol);
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tooltip);
        return pressed;
    }

    bool ToggleButton(const char* label, bool* value, const char* tooltip)
    {
        ImGuiID gid = ImGui::GetID(label);
        ImVec4 target = *value ? ImVec4(0.27f, 0.38f, 0.52f, 1.0f) : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
        ImVec4 bg = iam_tween_color(gid, ImHashStr("bg"), target, 0.15f, iam_ease_preset(iam_ease_out_quad),
                                    iam_policy_crossfade, iam_col_oklab, ImGui::GetIO().DeltaTime, target);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bg.x + 0.06f, bg.y + 0.06f, bg.z + 0.06f, 1.0f));
        bool pressed = ImGui::Button(label);
        ImGui::PopStyleColor(2);
        if (pressed) *value = !*value;
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", tooltip);
        return pressed;
    }

    void PropertyLabel(const char* label)
    {
        const float labelWidth = std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.38f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    bool Vec3Field(const char* label, float* values, float speed, float resetValue, const char* format, bool directInput)
    {
        bool changed = false;
        ImGui::PushID(label);
        const float labelWidth = std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.38f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (ImGui::BeginPopupContextItem("reset"))
        {
            if (ImGui::MenuItem("Reset"))
            {
                values[0] = values[1] = values[2] = resetValue;
                changed = true;
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(labelWidth);

        const float avail = ImGui::GetContentRegionAvail().x;
        const float letterW = ImGui::CalcTextSize("X").x + 6.0f;
        const float fieldW = (avail - letterW * 3.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        static const char* axes[3] = { "X", "Y", "Z" };
        for (int i = 0; i < 3; ++i)
        {
            if (i > 0) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(AxisColor(i)));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(axes[i]);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 2.0f);
            ImGui::SetNextItemWidth(fieldW);
            ImGui::PushID(i);
            changed |= directInput ? ImGui::InputFloat("##v", &values[i], 0.0f, 0.0f, format)
                                   : ImGui::DragFloat("##v", &values[i], speed, 0.0f, 0.0f, format);
            ImGui::PopID();
        }
        ImGui::PopID();
        return changed;
    }

    bool ComponentHeader(const char* label, Icon icon, bool* enabled, bool* removeRequested, bool defaultOpen)
    {
        ImGui::PushID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        ImGuiID openId = ImGui::GetID("open");
        bool open = storage->GetBool(openId, defaultOpen);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float h = ImGui::GetFrameHeight() + 2.0f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        pos.x -= ImGui::GetStyle().WindowPadding.x;
        const float width = ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x * 2.0f;
        ImVec2 max(pos.x + width, pos.y + h);

        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##hdr", ImVec2(ImGui::GetContentRegionAvail().x, h)))
        {
            open = !open;
            storage->SetBool(openId, open);
        }
        bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(pos, max, hovered ? IM_COL32(72, 72, 72, 255) : IM_COL32(62, 62, 62, 255));
        dl->AddLine(pos, ImVec2(max.x, pos.y), IM_COL32(26, 26, 26, 255));

        float x = pos.x + ImGui::GetStyle().WindowPadding.x;
        const float cy = pos.y + h * 0.5f;
        ImGui::RenderArrow(dl, ImVec2(x, cy - ImGui::GetFontSize() * 0.5f), IM_COL32(200, 200, 200, 255),
                           open ? ImGuiDir_Down : ImGuiDir_Right, 0.8f);
        x += ImGui::GetFontSize();
        DrawIcon(dl, icon, ImVec2(x + 8.0f, cy), 14.0f, IM_COL32(190, 190, 190, 255));
        x += 20.0f;

        if (enabled)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, pos.y + 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::Checkbox("##enabled", enabled);
            ImGui::PopStyleVar();
            x += ImGui::GetFrameHeight();
        }
        if (g_BoldFont) ImGui::PushFont(g_BoldFont, 0.0f);
        dl->AddText(ImVec2(x + 2.0f, cy - ImGui::GetFontSize() * 0.5f), IM_COL32(220, 220, 220, 255), label);
        if (g_BoldFont) ImGui::PopFont();

        if (removeRequested)
        {
            ImGui::SetCursorScreenPos(ImVec2(max.x - h - 4.0f, pos.y + 1.0f));
            if (ImGui::InvisibleButton("##menu", ImVec2(h - 2.0f, h - 2.0f)))
                ImGui::OpenPopup("component_menu");
            DrawIcon(dl, Icon::Menu, ImVec2(max.x - h * 0.5f - 5.0f, cy), 14.0f,
                     ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 170, 170, 255));
            if (ImGui::BeginPopup("component_menu"))
            {
                if (ImGui::MenuItem("Remove Component")) *removeRequested = true;
                ImGui::EndPopup();
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(pos.x + ImGui::GetStyle().WindowPadding.x, max.y + 4.0f));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
        return open;
    }
}
