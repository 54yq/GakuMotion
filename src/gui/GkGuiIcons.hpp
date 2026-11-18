#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>

// Small original vector symbols: no font atlas, private-use glyphs or external
// asset dependency. Coordinates share a 16-unit grid and inherit text color.
namespace GakuMotion::GuiIcons
{
    enum class Icon { Add, Refresh, Folder, Edit, Trash, Undo, Preview, Play,
        Save, Reset, Library, Motion, Camera, Stage, Settings, Developer };

    inline void Draw(ImDrawList* draw, Icon icon, ImVec2 origin, float size, ImU32 color)
    {
        const float scale = size / 16.0f;
        const float stroke = (std::max)(1.0f, 1.5f * scale);
        const auto point = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
        const auto line = [&](float x1, float y1, float x2, float y2) {
            draw->AddLine(point(x1, y1), point(x2, y2), color, stroke);
        };
        const auto rect = [&](float x1, float y1, float x2, float y2, float rounding = 1.0f) {
            draw->AddRect(point(x1, y1), point(x2, y2), color, rounding * scale, 0, stroke);
        };
        switch (icon)
        {
        case Icon::Add:
            line(8, 3, 8, 13); line(3, 8, 13, 8); break;
        case Icon::Reset:
        case Icon::Refresh:
        case Icon::Undo:
            draw->PathArcTo(point(8, 8), 5.0f * scale, -2.5f, 2.5f, 18);
            draw->PathStroke(color, 0, stroke);
            line(3, 2, 3, 6); line(3, 6, 7, 6);
            if (icon == Icon::Refresh) { line(13, 10, 13, 14); line(9, 10, 13, 10); }
            break;
        case Icon::Folder:
            line(2, 5, 2, 13); line(2, 13, 14, 13); line(14, 13, 14, 5);
            line(14, 5, 8, 5); line(8, 5, 6, 3); line(6, 3, 2, 3); line(2, 3, 2, 5);
            break;
        case Icon::Edit:
            line(3, 10, 10, 3); line(10, 3, 13, 6); line(13, 6, 6, 13);
            line(6, 13, 2, 14); line(2, 14, 3, 10); line(9, 4, 12, 7); break;
        case Icon::Trash:
            line(2, 4, 14, 4); line(6, 2, 10, 2); line(4, 6, 5, 14);
            line(5, 14, 11, 14); line(11, 14, 12, 6); line(7, 7, 7, 11); line(9, 7, 9, 11); break;
        case Icon::Preview:
            line(1, 8, 5, 4); line(5, 4, 11, 4); line(11, 4, 15, 8);
            line(15, 8, 11, 12); line(11, 12, 5, 12); line(5, 12, 1, 8);
            draw->AddCircle(point(8, 8), 2.0f * scale, color, 12, stroke); break;
        case Icon::Play:
            draw->AddTriangleFilled(point(5, 3), point(13, 8), point(5, 13), color); break;
        case Icon::Save:
            rect(2, 2, 14, 14); rect(5, 2, 11, 6, 0); rect(5, 10, 11, 14, 0); break;
        case Icon::Library:
            rect(2, 3, 5, 14, 0); rect(7, 3, 10, 14, 0); line(12, 3, 14, 14); break;
        case Icon::Motion:
            draw->AddCircle(point(9, 2.5f), 1.5f * scale, color, 12, stroke);
            line(8, 5, 7, 9); line(7, 9, 4, 14); line(7, 9, 11, 13);
            line(4, 6, 8, 5); line(8, 5, 12, 7); break;
        case Icon::Camera:
            rect(2, 5, 14, 13, 2); line(5, 5, 6, 3); line(6, 3, 10, 3); line(10, 3, 11, 5);
            draw->AddCircle(point(8, 9), 2.5f * scale, color, 16, stroke); break;
        case Icon::Stage:
            line(2, 3, 14, 3); line(2, 3, 2, 13); line(14, 3, 14, 13); line(2, 13, 14, 13);
            line(5, 3, 4, 9); line(11, 3, 12, 9); line(4, 9, 2, 10); line(12, 9, 14, 10); break;
        case Icon::Settings:
            draw->AddCircle(point(8, 8), 4.5f * scale, color, 20, stroke);
            draw->AddCircle(point(8, 8), 1.5f * scale, color, 12, stroke);
            for (int i = 0; i < 8; ++i)
            {
                const float angle = i * 0.78539816f;
                line(8 + 4.5f * std::cos(angle), 8 + 4.5f * std::sin(angle),
                    8 + 6.5f * std::cos(angle), 8 + 6.5f * std::sin(angle));
            }
            break;
        case Icon::Developer:
            line(5, 4, 1, 8); line(1, 8, 5, 12); line(11, 4, 15, 8); line(15, 8, 11, 12);
            line(9, 2, 7, 14); break;
        }
    }

    inline ImVec2 ButtonSize(const char* label, ImVec2 minimumSize = ImVec2(0, 0))
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* labelEnd = std::strstr(label, "##");
        if (!labelEnd) labelEnd = label + std::strlen(label);
        const ImVec2 textSize = ImGui::CalcTextSize(label, labelEnd);
        const float groupWidth = ImGui::GetFontSize() * 1.20f + textSize.x;
        ImVec2 size = minimumSize;
        if (size.x >= 0.0f) size.x = (std::max)(size.x, groupWidth + 2.0f * style.FramePadding.x);
        size.y = (std::max)(size.y, ImGui::GetFrameHeight());
        return size;
    }

    inline void SameLineIfRoom(const char* label, ImVec2 minimumSize = ImVec2(0, 0))
    {
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (right - ImGui::GetItemRectMax().x >= ButtonSize(label, minimumSize).x + ImGui::GetStyle().ItemSpacing.x)
            ImGui::SameLine();
    }

    // Keep the original label/ID and native Button behavior (keyboard navigation,
    // hover, activation, disabled state and test hooks). Size is a minimum;
    // grow with the active font so a symbol never displaces or hides the label.
    inline bool Button(const char* label, Icon icon, ImVec2 minimumSize = ImVec2(0, 0))
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* labelEnd = std::strstr(label, "##");
        if (!labelEnd) labelEnd = label + std::strlen(label);
        const ImVec2 textSize = ImGui::CalcTextSize(label, labelEnd);
        const float iconSize = ImGui::GetFontSize() * 0.90f;
        const float gap = ImGui::GetFontSize() * 0.30f;
        const float groupWidth = iconSize + gap + textSize.x;
        const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
        ImVec4 hiddenText = style.Colors[ImGuiCol_Text];
        hiddenText.w = 0.0f;
        ImGui::PushStyleColor(ImGuiCol_Text, hiddenText);
        const bool pressed = ImGui::Button(label, ButtonSize(label, minimumSize));
        ImGui::PopStyleColor();
        if (!ImGui::IsItemVisible()) return pressed;

        const ImVec2 start = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
        const float x = start.x + (std::max)(style.FramePadding.x, (end.x - start.x - groupWidth) * style.ButtonTextAlign.x);
        const float y = start.y + (end.y - start.y - ImGui::GetFontSize()) * style.ButtonTextAlign.y;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(start, end, true);
        Draw(draw, icon, ImVec2(x, y + (ImGui::GetFontSize() - iconSize) * 0.5f), iconSize, color);
        draw->AddText(ImVec2(x + iconSize + gap, y), color, label, labelEnd);
        draw->PopClipRect();
        return pressed;
    }
}
