#include "EditorToolbarModel.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<EditorToolbarButton, EditorToolbarModel::ButtonCount>
    ToolbarButtons{{
        {EditorToolbarAction::Pencil, EditorToolbarGroup::Sculpt,
         "Smart Tool", "Edit voxels with Smart Brush", EditorInputCommand::ToolPencil,
         ActiveVoxelTool::Pencil},
        {EditorToolbarAction::Selection, EditorToolbarGroup::Selection,
         "Selection", "Select voxels", EditorInputCommand::ToolSelection,
         ActiveVoxelTool::Selection},
        {EditorToolbarAction::Transform, EditorToolbarGroup::Transform,
         "Transform", "Move the selected voxels", EditorInputCommand::ToolMove,
         ActiveVoxelTool::Move}
    }};
}

std::span<const EditorToolbarButton, EditorToolbarModel::PrimaryButtonCount>
EditorToolbarModel::PrimaryButtons() noexcept
{
    return std::span<const EditorToolbarButton, PrimaryButtonCount>(
        ToolbarButtons.data(), PrimaryButtonCount);
}

const std::array<EditorToolbarButton, EditorToolbarModel::ButtonCount>&
EditorToolbarModel::Buttons() noexcept
{
    return ToolbarButtons;
}

bool EditorToolbarModel::IsEnabled(
    const EditorToolbarButton& button,
    const EditorToolbarState& state) noexcept
{
    if (button.Action == EditorToolbarAction::Transform)
        return state.HasDocument && state.CanMoveSelection;
    if (button.Action == EditorToolbarAction::Duplicate)
        return state.HasDocument && state.CanDuplicateSelection;
    if (button.Action == EditorToolbarAction::Rotate)
        return state.HasDocument && state.CanRotateSelection;
    if (button.Action == EditorToolbarAction::Scale)
        return state.HasDocument && state.CanScaleSelection;
    if (button.Action == EditorToolbarAction::Mirror)
        return state.HasDocument && state.CanMirrorSelection;
    if (button.Action == EditorToolbarAction::Align)
        return state.HasDocument && state.CanAlignSelection;
    return state.HasDocument;
}

bool EditorToolbarModel::IsActive(
    const EditorToolbarButton& button,
    const EditorToolbarState& state) noexcept
{
    if (!state.HasDocument) return false;
    if (button.Action == EditorToolbarAction::Pencil)
        return state.ActiveTool == ActiveVoxelTool::Pencil;
    if (button.Action == EditorToolbarAction::Transform)
        return state.ActiveTool == ActiveVoxelTool::Move ||
            state.ActiveTool == ActiveVoxelTool::Duplicate ||
            state.ActiveTool == ActiveVoxelTool::Rotate ||
            state.ActiveTool == ActiveVoxelTool::Mirror ||
            state.ActiveTool == ActiveVoxelTool::Scale ||
            state.ActiveTool == ActiveVoxelTool::Align;
    return button.Tool != ActiveVoxelTool::None &&
        button.Tool == state.ActiveTool;
}

EditorToolbarLayout EditorToolbarModel::CalculateLayout(
    const float availableWidth,
    const float fontSize) noexcept
{
    const float safeWidth = std::max(availableWidth, 1.0F);
    const float safeFontSize = std::max(fontSize, 1.0F);
    const float preferredButton = std::clamp(
        std::ceil(safeFontSize * 2.05F), 32.0F, 40.0F);
    const float regularSpacing = std::clamp(
        std::floor(safeFontSize * 0.32F), 4.0F, 7.0F);
    const float groupSpacing = std::clamp(
        std::floor(safeFontSize * 0.75F), 10.0F, 16.0F);
    const float singleRowWidth = preferredButton * PrimaryButtonCount +
        regularSpacing * static_cast<float>(PrimaryButtonCount - 1U) +
        groupSpacing * 2.0F;
    if (singleRowWidth <= safeWidth)
        return {preferredButton, regularSpacing, groupSpacing, false};

    constexpr float MinimumButtonSize = 24.0F;
    const float wrappedButton = std::clamp(
        (safeWidth - regularSpacing * 2.0F) / 3.0F,
        MinimumButtonSize, preferredButton);
    return {wrappedButton, regularSpacing, groupSpacing, true};
}

} // namespace VoxelForge::Editor
