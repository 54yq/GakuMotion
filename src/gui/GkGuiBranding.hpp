#pragma once

#if defined(GKMOTION_ENABLE_IMGUI) || defined(HAS_IMGUI)
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include "GkGuiThemeColors.hpp"
#include "generated/GkGuiBrandingMesh.hpp"

namespace GakuMotion::GuiBranding {

// The SVG's transparent margins are cropped in the generated mesh. This helper
// consumes only the host's existing ImGui font white pixel, never a logo texture.
inline void DrawIcon(ImDrawList* drawList, const ImVec2& origin, float width, bool isLight) {
    if (!drawList || !std::isfinite(width) || width <= 0.0f) return;
    const float scale = width / Mesh::Width;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const bool antialias = (drawList->Flags & ImDrawListFlags_AntiAliasedFill) != 0;
    for (const auto& part : Mesh::Parts) {
        const int stride = antialias ? 2 : 1;
        const int vertexCount = part.vertexCount * stride;
        // PrimReserve handles VtxOffset-enabled backends. A very full legacy
        // 16-bit draw list may safely omit the optional brand instead of wrapping.
        if (sizeof(ImDrawIdx) == 2 && !(drawList->Flags & ImDrawListFlags_AllowVtxOffset) &&
            drawList->_VtxCurrentIdx + vertexCount > 65535u) return;
        drawList->PrimReserve(part.indexCount + (antialias ? part.edgeCount * 6 : 0), vertexCount);
        const unsigned int base = drawList->_VtxCurrentIdx; // after possible VtxOffset rollover
        for (int i = 0; i < part.vertexCount; ++i) {
            const auto& v = Mesh::Vertices[part.vertexOffset + i];
            const auto& rgb = isLight ? v.light : v.dark;
            const ImU32 color = ImGui::GetColorU32(ImVec4(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, 1.0f));
            const ImVec2 position(origin.x + v.x * scale, origin.y + v.y * scale);
            if (!antialias) {
                drawList->PrimWriteVtx(position, uv, color);
                continue;
            }
            // Only real silhouette/hole edges get a fringe. Opposite-edge
            // clearance prevents tiny rings and the thin M trajectory inverting.
            const float coverage = std::clamp(v.width * scale, 0.0f, 1.0f);
            const float inset = coverage * 0.5f;
            const float outset = 1.0f - inset;
            const unsigned int alpha = (color >> IM_COL32_A_SHIFT) & 0xffu;
            const ImU32 inner = (color & ~IM_COL32_A_MASK) |
                (static_cast<ImU32>(alpha * coverage + 0.5f) << IM_COL32_A_SHIFT);
            drawList->PrimWriteVtx(ImVec2(position.x - v.nx * inset, position.y - v.ny * inset), uv, inner);
            drawList->PrimWriteVtx(ImVec2(position.x + v.nx * outset, position.y + v.ny * outset), uv, color & ~IM_COL32_A_MASK);
        }
        for (int i = 0; i < part.indexCount; ++i)
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + Mesh::Indices[part.indexOffset + i] * stride));
        if (antialias) {
            for (int i = 0; i < part.edgeCount; ++i) {
                const auto& edge = Mesh::Edges[part.edgeOffset + i];
                const unsigned int a = base + edge.a * 2, b = base + edge.b * 2;
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(a));
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(b));
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1));
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(a));
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(b + 1));
                drawList->PrimWriteIdx(static_cast<ImDrawIdx>(a + 1));
            }
        }
    }
}

inline void DrawSidebarIcon(float width) {
    if (!std::isfinite(width) || width <= 0.0f) return;
    ImVec2 tempPos = ImGui::GetCursorScreenPos();
    tempPos.x += 15.0f;
    DrawIcon(ImGui::GetWindowDrawList(), tempPos, width, GuiTheme::IsLight());
    ImGui::Dummy(ImVec2(width + 30.0f, width * Mesh::Height / Mesh::Width));
}

} // namespace GakuMotion::GuiBranding
#endif
