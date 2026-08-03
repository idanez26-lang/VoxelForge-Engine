#pragma once

// VF-0260 (nettoyage post-lot 7d) : petits assistants ImGui de l'atelier,
// autrefois dupliqués en copies anonymes dans EditorWorkspace.cpp,
// EditorWorkspaceViewportPanel.cpp et EditorWorkspaceSmoke.cpp — une seule
// source de vérité pour éviter toute divergence silencieuse.

#include "Selection/SelectionHandleModel.h"

#include <imgui.h>

#include <cmath>
#include <optional>
#include <string_view>

namespace VoxelForge::Editor
{

inline void DrawErrorMessage(const std::string_view error)
{
    if (error.empty())
    {
        return;
    }

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(0.95F, 0.35F, 0.30F, 1.0F));
    ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
    ImGui::PopStyleColor();
}

inline void DrawTooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

inline void DrawSelectionHandles(
    const SelectionHandles& handles,
    const std::optional<SelectionFace> hoveredFace,
    const SelectionFace activeFace)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float NormalHalfSize = 4.5F;
    constexpr float ActiveHalfSize = 6.0F;
    for (const SelectionHandle& handle : handles)
    {
        if (!handle.Visible) continue;
        const bool active = handle.Face == activeFace;
        const bool hovered = hoveredFace && *hoveredFace == handle.Face;
        const float halfSize = active ? ActiveHalfSize : NormalHalfSize;
        const ImVec2 minimum{
            handle.ScreenPosition.X - halfSize,
            handle.ScreenPosition.Y - halfSize};
        const ImVec2 maximum{
            handle.ScreenPosition.X + halfSize,
            handle.ScreenPosition.Y + halfSize};
        const ImU32 fill = active
            ? IM_COL32(255, 194, 92, 255)
            : hovered ? IM_COL32(104, 255, 220, 255)
                      : IM_COL32(18, 45, 50, 245);
        const ImU32 outline = active || hovered
            ? IM_COL32(248, 255, 253, 255)
            : IM_COL32(78, 232, 202, 255);
        drawList->AddRectFilled(minimum, maximum, fill, 1.5F);
        drawList->AddRect(minimum, maximum, IM_COL32(4, 8, 12, 255), 1.5F,
            0, 3.0F);
        drawList->AddRect(minimum, maximum, outline, 1.5F, 0, 1.0F);
    }
}

inline void SetSelectionHandleCursor(const SelectionHandle& handle)
{
    const float horizontal = std::abs(handle.ScreenAxisPerVoxel.X);
    const float vertical = std::abs(handle.ScreenAxisPerVoxel.Y);
    if (horizontal > vertical * 1.5F)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (vertical > horizontal * 1.5F)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    else
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
}

} // namespace VoxelForge::Editor
