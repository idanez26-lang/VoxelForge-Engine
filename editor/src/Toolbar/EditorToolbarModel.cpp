#include "EditorToolbarModel.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<EditorToolbarButton, EditorToolbarModel::ButtonCount>
    ToolbarButtons{{
        {EditorToolbarAction::Save, EditorToolbarGroup::File,
         "Save", "Save the active voxel model", EditorInputCommand::FileSave,
         ActiveVoxelTool::None},
        {EditorToolbarAction::Pencil, EditorToolbarGroup::Sculpt,
         "Pencil", "Draw voxels", EditorInputCommand::ToolPencil,
         ActiveVoxelTool::Pencil},
        {EditorToolbarAction::Eraser, EditorToolbarGroup::Sculpt,
         "Eraser", "Remove voxels", EditorInputCommand::ToolEraser,
         ActiveVoxelTool::Eraser},
        {EditorToolbarAction::Paint, EditorToolbarGroup::Sculpt,
         "Paint", "Recolor a connected area", EditorInputCommand::ToolFill,
         ActiveVoxelTool::Fill},
        {EditorToolbarAction::Selection, EditorToolbarGroup::Selection,
         "Selection", "Select voxels", EditorInputCommand::ToolSelection,
         ActiveVoxelTool::Selection},
        {EditorToolbarAction::Move, EditorToolbarGroup::Transform,
         "Move", "Move the selected voxels", EditorInputCommand::ToolMove,
         ActiveVoxelTool::Move},
        {EditorToolbarAction::Rotate, EditorToolbarGroup::Transform,
         "Rotate", "Rotate the selected voxels by 90 degrees",
         EditorInputCommand::ToolRotate, ActiveVoxelTool::Rotate},
        {EditorToolbarAction::Scale, EditorToolbarGroup::Transform,
         "Scale", "Scale the selected voxels by 2",
         EditorInputCommand::ToolScale, ActiveVoxelTool::Scale},
        {EditorToolbarAction::Box, EditorToolbarGroup::Construction,
         "Box", "Create a filled voxel box", EditorInputCommand::ToolBox,
         ActiveVoxelTool::Box},
        {EditorToolbarAction::Line, EditorToolbarGroup::Construction,
         "Line", "Create a voxel line", EditorInputCommand::ToolLine,
         ActiveVoxelTool::Line},
        {EditorToolbarAction::Sphere, EditorToolbarGroup::Construction,
         "Sphere", "Create a filled voxel sphere",
         EditorInputCommand::ToolSphere, ActiveVoxelTool::Sphere},
        {EditorToolbarAction::Duplicate, EditorToolbarGroup::Manipulation,
         "Duplicate", "Duplicate the selected voxels",
         EditorInputCommand::ToolDuplicate, ActiveVoxelTool::Duplicate},
        {EditorToolbarAction::Mirror, EditorToolbarGroup::Manipulation,
         "Mirror", "Mirror the selected voxels on X or Z",
         EditorInputCommand::ToolMirror, ActiveVoxelTool::Mirror},
        {EditorToolbarAction::Align, EditorToolbarGroup::Manipulation,
         "Align", "Align the selected voxels to a model face",
         EditorInputCommand::ToolAlign, ActiveVoxelTool::Align}
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
    if (button.Action == EditorToolbarAction::Save) return state.CanSave;
    if (button.Action == EditorToolbarAction::Move)
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
    return state.HasDocument && button.Tool != ActiveVoxelTool::None &&
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
        groupSpacing * 3.0F;
    if (singleRowWidth <= safeWidth)
        return {preferredButton, regularSpacing, groupSpacing, false};

    constexpr float MinimumButtonSize = 24.0F;
    const float wrappedButton = std::clamp(
        (safeWidth - regularSpacing * 2.0F) / 3.0F,
        MinimumButtonSize, preferredButton);
    return {wrappedButton, regularSpacing, groupSpacing, true};
}

} // namespace VoxelForge::Editor
