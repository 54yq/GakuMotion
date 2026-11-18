#pragma once
#include "../GkGmlClient.hpp"
#if defined(GKMOTION_ENABLE_IMGUI) || defined(HAS_IMGUI)
#include <imgui.h>

namespace GakuMotion::GuiTheme {

inline bool IsLight() {
    if (GakuMotion::host::GetCurrentTheme() == GAKU_UI_THEME_PAPER_AND_INK) return true;
    if (ImGui::GetCurrentContext() && ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x > 0.6f) return true;
    return false;
}

// Green / Success notice text
inline ImVec4 SuccessColor() {
    return IsLight() ? ImVec4(0.10f, 0.52f, 0.18f, 1.0f) : ImVec4(0.40f, 0.95f, 0.45f, 1.0f);
}

// Amber / Orange / Warning notice text
inline ImVec4 WarningColor() {
    return IsLight() ? ImVec4(0.72f, 0.38f, 0.05f, 1.0f) : ImVec4(1.00f, 0.78f, 0.25f, 1.0f);
}

// Red / Error notice text
inline ImVec4 ErrorColor() {
    return IsLight() ? ImVec4(0.78f, 0.15f, 0.15f, 1.0f) : ImVec4(1.00f, 0.45f, 0.45f, 1.0f);
}

// Cyan / Sky / Info notice text
inline ImVec4 InfoColor() {
    return IsLight() ? ImVec4(0.12f, 0.40f, 0.72f, 1.0f) : ImVec4(0.45f, 0.78f, 1.00f, 1.0f);
}

// Gold / Yellow / Bone highlight text
inline ImVec4 HighlightColor() {
    return IsLight() ? ImVec4(0.58f, 0.42f, 0.05f, 1.0f) : ImVec4(1.00f, 0.88f, 0.35f, 1.0f);
}

// 标签专用颜色（深浅主题高对比度适配）
inline ImVec4 TagMotionColor() {
    return IsLight() ? ImVec4(0.08f, 0.38f, 0.78f, 1.0f) : ImVec4(0.40f, 0.80f, 1.00f, 1.0f);
}

inline ImVec4 TagFacialColor() {
    return IsLight() ? ImVec4(0.72f, 0.18f, 0.52f, 1.0f) : ImVec4(1.00f, 0.60f, 0.85f, 1.0f);
}

inline ImVec4 TagCameraColor() {
    return IsLight() ? ImVec4(0.75f, 0.42f, 0.05f, 1.0f) : ImVec4(0.95f, 0.82f, 0.35f, 1.0f);
}

inline ImVec4 TagAudioColor() {
    return IsLight() ? ImVec4(0.10f, 0.52f, 0.20f, 1.0f) : ImVec4(0.45f, 0.90f, 0.50f, 1.0f);
}

// 媒体控制按钮图标颜色（深浅主题高对比度适配）
inline ImVec4 TransportIconColor(bool hovered, bool active) {
    if (IsLight()) {
        if (active && hovered) return ImVec4(0.00f, 0.35f, 0.75f, 1.0f);
        if (hovered) return ImVec4(0.08f, 0.45f, 0.88f, 1.0f);
        return ImVec4(0.20f, 0.22f, 0.26f, 1.0f);
    } else {
        if (active && hovered) return ImVec4(0.50f, 0.85f, 1.00f, 1.0f);
        if (hovered) return ImVec4(0.35f, 0.75f, 1.00f, 1.0f);
        return ImVec4(0.85f, 0.88f, 0.92f, 1.0f);
    }
}

} // namespace GakuMotion::GuiTheme
#endif
