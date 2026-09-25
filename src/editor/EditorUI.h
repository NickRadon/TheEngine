#pragma once

#include <imgui.h>

// Unity-like theme, fonts and small vector icons drawn with ImDrawList (no icon font needed).
namespace EditorUI
{
    void ApplyUnityTheme();
    void LoadFonts(float dpiScale);
    ImFont* BoldFont();

    // Lerp editor chrome colors towards the play-mode tint (t in 0..1).
    void ApplyPlayModeTint(float t);

    enum class Icon
    {
        Hand, Move, Rotate, Scale, Rect, Transform,
        Play, Pause, Step,
        Sun, Camera, Cube, Empty, Scene, Folder, File,
        Grid, Gizmos, Info, Warning, Error, Search, Plus, Eye, Menu, Sky
    };

    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

    // Toolbar-style square icon button with animated (ImAnim) hover/selected background.
    bool IconButton(const char* id, Icon icon, bool selected, const char* tooltip, ImVec2 size = ImVec2(0, 0));

    // Toggle button with text; animated background.
    bool ToggleButton(const char* label, bool* value, const char* tooltip = nullptr);

    // Unity-style labeled vector field (X/Y/Z colored labels). Returns true when edited.
    bool Vec3Field(const char* label, float* values, float speed, float resetValue, const char* format = "%.3g");

    // Two-column property row helpers (label on the left, widget fills the rest).
    void PropertyLabel(const char* label);

    // Unity-like component header; returns open state. `enabled` may be null.
    bool ComponentHeader(const char* label, Icon icon, bool* enabled, bool* removeRequested, bool defaultOpen = true);

    ImU32 AxisColor(int axis, float alpha = 1.0f);
}
