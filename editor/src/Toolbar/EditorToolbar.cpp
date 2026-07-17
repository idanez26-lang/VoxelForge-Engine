#include "EditorToolbar.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>

namespace VoxelForge::Editor
{
namespace
{
constexpr ImU32 AccentColor = IM_COL32(49, 112, 178, 255);
constexpr ImU32 AccentHoverColor = IM_COL32(61, 132, 207, 255);
constexpr ImU32 PressedColor = IM_COL32(38, 91, 148, 255);
constexpr ImU32 IdleColor = IM_COL32(43, 47, 54, 255);
constexpr ImU32 IdleHoverColor = IM_COL32(57, 63, 72, 255);
constexpr ImU32 DisabledColor = IM_COL32(33, 36, 41, 180);
constexpr ImU32 IconColor = IM_COL32(226, 232, 239, 255);
constexpr ImU32 DisabledIconColor = IM_COL32(112, 118, 126, 210);
constexpr ImU32 ActiveBorderColor = IM_COL32(225, 239, 255, 255);
constexpr ImU32 SeparatorColor = IM_COL32(90, 96, 106, 150);
constexpr float CornerRadius = 5.0F;
constexpr float BorderThickness = 1.0F;
constexpr float ActiveIndicatorHeight = 3.0F;

void DrawSaveIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    drawList.AddRect(minimum, maximum, color, 1.5F, 0, thickness);
    const float width = maximum.x - minimum.x;
    drawList.AddRect(
        {minimum.x + width * 0.22F, minimum.y},
        {maximum.x - width * 0.18F, minimum.y + width * 0.34F},
        color, 0.0F, 0, thickness);
    drawList.AddRect(
        {minimum.x + width * 0.24F, maximum.y - width * 0.36F},
        {maximum.x - width * 0.24F, maximum.y},
        color, 0.0F, 0, thickness);
}

void DrawPencilIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    const ImVec2 start{minimum.x + 2.0F, maximum.y - 2.0F};
    const ImVec2 end{maximum.x - 3.0F, minimum.y + 3.0F};
    drawList.AddLine(start, end, color, thickness * 2.3F);
    drawList.AddTriangleFilled(
        {minimum.x, maximum.y}, {minimum.x + 5.0F, maximum.y - 1.0F},
        {minimum.x + 1.0F, maximum.y - 5.0F}, color);
}

void DrawEraserIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    const std::array<ImVec2, 4> points{{
        {minimum.x + 2.0F, maximum.y - 5.0F},
        {minimum.x + 8.0F, minimum.y + 2.0F},
        {maximum.x - 1.0F, minimum.y + 8.0F},
        {maximum.x - 7.0F, maximum.y - 1.0F}}};
    drawList.AddPolyline(points.data(), static_cast<int>(points.size()), color,
        ImDrawFlags_Closed, thickness);
    drawList.AddLine(points[0], points[3], color, thickness);
}

void DrawFillIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    const std::array<ImVec2, 4> bucket{{
        {minimum.x + 2.0F, minimum.y + 7.0F},
        {minimum.x + 9.0F, minimum.y + 1.0F},
        {maximum.x - 2.0F, minimum.y + 9.0F},
        {minimum.x + 8.0F, maximum.y - 2.0F}}};
    drawList.AddPolyline(bucket.data(), static_cast<int>(bucket.size()), color,
        ImDrawFlags_Closed, thickness);
    drawList.AddLine(
        {minimum.x + 2.0F, maximum.y - 1.0F},
        {maximum.x - 5.0F, maximum.y - 1.0F}, color, thickness);
    drawList.AddCircleFilled(
        {maximum.x - 2.0F, maximum.y - 4.0F}, 2.0F, color, 8);
}

void DrawBoxIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    const ImVec2 top{(minimum.x + maximum.x) * 0.5F, minimum.y};
    const ImVec2 left{minimum.x, minimum.y + 5.0F};
    const ImVec2 right{maximum.x, minimum.y + 5.0F};
    const ImVec2 middle{top.x, minimum.y + 10.0F};
    const ImVec2 bottom{top.x, maximum.y};
    drawList.AddLine(top, left, color, thickness);
    drawList.AddLine(top, right, color, thickness);
    drawList.AddLine(left, middle, color, thickness);
    drawList.AddLine(right, middle, color, thickness);
    drawList.AddLine(left, {left.x, maximum.y - 5.0F}, color, thickness);
    drawList.AddLine(right, {right.x, maximum.y - 5.0F}, color, thickness);
    drawList.AddLine(middle, bottom, color, thickness);
    drawList.AddLine({left.x, maximum.y - 5.0F}, bottom, color, thickness);
    drawList.AddLine({right.x, maximum.y - 5.0F}, bottom, color, thickness);
}

void DrawLineIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    drawList.AddLine(
        {minimum.x + 1.0F, maximum.y - 1.0F},
        {maximum.x - 1.0F, minimum.y + 1.0F}, color, thickness * 1.5F);
    drawList.AddCircleFilled(
        {minimum.x + 1.0F, maximum.y - 1.0F}, thickness, color, 8);
    drawList.AddCircleFilled(
        {maximum.x - 1.0F, minimum.y + 1.0F}, thickness, color, 8);
}

void DrawSphereIcon(ImDrawList& drawList, const ImVec2 minimum,
    const ImVec2 maximum, const ImU32 color, const float thickness)
{
    const ImVec2 center{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F};
    const float radius = (maximum.x - minimum.x) * 0.48F;
    drawList.AddCircle(center, radius, color, 24, thickness);
    drawList.AddEllipse(center, {radius * 0.45F, radius}, color,
        0.0F, 20, thickness);
    drawList.AddEllipse(center, {radius, radius * 0.35F}, color,
        0.0F, 20, thickness);
}

bool DrawIcon(
    ImDrawList& drawList,
    const EditorToolbarAction action,
    const ImVec2 minimum,
    const ImVec2 maximum,
    const ImU32 color,
    const float thickness)
{
    switch (action)
    {
    case EditorToolbarAction::Save:
        DrawSaveIcon(drawList, minimum, maximum, color, thickness); return true;
    case EditorToolbarAction::Pencil:
        DrawPencilIcon(drawList, minimum, maximum, color, thickness); return true;
    case EditorToolbarAction::Eraser:
        DrawEraserIcon(drawList, minimum, maximum, color, thickness); return true;
    case EditorToolbarAction::Fill:
        DrawFillIcon(drawList, minimum, maximum, color, thickness); return true;
    case EditorToolbarAction::Box:
        DrawBoxIcon(drawList, minimum, maximum, color, thickness); return true;
    case EditorToolbarAction::Line:
        DrawLineIcon(drawList, minimum, maximum, color, thickness); return true;
    case EditorToolbarAction::Sphere:
        DrawSphereIcon(drawList, minimum, maximum, color, thickness); return true;
    }
    return false;
}

void DrawTooltip(
    const EditorToolbarButton& button,
    const EditorInputService& inputService,
    const bool enabled)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(button.Name.data(), button.Name.data() + button.Name.size());
    ImGui::TextDisabled("%.*s", static_cast<int>(button.Description.size()),
        button.Description.data());
    const std::string_view shortcut = inputService.ShortcutLabel(button.Command);
    if (!shortcut.empty())
        ImGui::TextDisabled("Shortcut: %.*s",
            static_cast<int>(shortcut.size()), shortcut.data());
    if (!enabled)
        ImGui::TextDisabled(button.Action == EditorToolbarAction::Save
            ? "No unsaved voxel model to save." : "Open a voxel model to use this tool.");
    ImGui::EndTooltip();
}

bool DrawButton(
    const EditorToolbarButton& button,
    const EditorToolbarState& state,
    const EditorInputService& inputService,
    const float size)
{
    const bool enabled = EditorToolbarModel::IsEnabled(button, state);
    const bool active = EditorToolbarModel::IsActive(button, state);
    const std::string identifier = "##Toolbar" + std::string(button.Name);
    ImGui::BeginDisabled(!enabled);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(
        identifier.c_str(), {size, size});
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool pressed = ImGui::IsItemActive();
    ImGui::EndDisabled();

    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 maximum{minimum.x + size, minimum.y + size};
    const ImU32 background = !enabled
        ? DisabledColor : pressed ? PressedColor : active
        ? (hovered ? AccentHoverColor : AccentColor)
        : (hovered ? IdleHoverColor : IdleColor);
    drawList.AddRectFilled(minimum, maximum, background, CornerRadius);
    drawList.AddRect(minimum, maximum,
        active ? ActiveBorderColor : IM_COL32(82, 88, 98, 180),
        CornerRadius, 0, active ? 1.5F : BorderThickness);
    if (active)
        drawList.AddRectFilled(
            {minimum.x + 3.0F, maximum.y - ActiveIndicatorHeight},
            {maximum.x - 3.0F, maximum.y}, ActiveBorderColor, 1.0F);

    const float iconPadding = std::max(6.0F, size * 0.24F);
    const ImVec2 iconMinimum{minimum.x + iconPadding, minimum.y + iconPadding};
    const ImVec2 iconMaximum{maximum.x - iconPadding, maximum.y - iconPadding};
    const ImU32 iconColor = enabled ? IconColor : DisabledIconColor;
    if (!DrawIcon(drawList, button.Action, iconMinimum, iconMaximum,
            iconColor, std::max(1.2F, size * 0.045F)))
    {
        const std::string fallback(1U, button.Name.empty() ? '?' : button.Name.front());
        const ImVec2 textSize = ImGui::CalcTextSize(fallback.c_str());
        drawList.AddText(
            {minimum.x + (size - textSize.x) * 0.5F,
             minimum.y + (size - textSize.y) * 0.5F},
            iconColor, fallback.c_str());
    }
    DrawTooltip(button, inputService, enabled);
    return enabled && clicked;
}

void DrawSeparator(const float height, const float width)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Dummy({width, height});
    ImGui::GetWindowDrawList()->AddLine(
        {cursor.x + width * 0.5F, cursor.y + 5.0F},
        {cursor.x + width * 0.5F, cursor.y + height - 5.0F},
        SeparatorColor, 1.0F);
}
}

void EditorToolbar::Draw(
    const EditorToolbarState& state,
    const EditorInputService& inputService,
    const EditorToolbarCallbacks& callbacks)
{
    const EditorToolbarLayout layout = EditorToolbarModel::CalculateLayout(
        ImGui::GetContentRegionAvail().x, ImGui::GetFontSize());
    const auto& buttons = EditorToolbarModel::Buttons();
    EditorToolbarGroup previousGroup = buttons.front().Group;
    bool first = true;
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing, ImVec2(layout.ItemSpacing, layout.ItemSpacing));
    for (const EditorToolbarButton& button : buttons)
    {
        const bool newGroup = !first && button.Group != previousGroup;
        if (newGroup)
        {
            if (layout.WrapGroups)
                ImGui::NewLine();
            else
            {
                ImGui::SameLine(0.0F, layout.ItemSpacing);
                DrawSeparator(layout.ButtonSize, layout.GroupSpacing);
                ImGui::SameLine(0.0F, layout.ItemSpacing);
            }
        }
        else if (!first)
        {
            ImGui::SameLine(0.0F, layout.ItemSpacing);
        }

        if (DrawButton(button, state, inputService, layout.ButtonSize))
        {
            if (callbacks.ExecuteCommand)
                callbacks.ExecuteCommand(button.Command);
        }
        first = false;
        previousGroup = button.Group;
    }
    ImGui::PopStyleVar();
}

} // namespace VoxelForge::Editor
