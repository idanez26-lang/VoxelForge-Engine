#include "ToolManager.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<ToolDescriptor, ToolManager::ToolCount> ToolDefinitions{{
    {ActiveVoxelTool::Pencil, "Smart Tool", EditorInputCommand::ToolPencil,
        ToolPanelKind::Smart, ToolCursor::Crosshair},
    {ActiveVoxelTool::Eraser, "Erase", EditorInputCommand::ToolEraser,
        ToolPanelKind::Smart, ToolCursor::Crosshair},
    {ActiveVoxelTool::Fill, "Paint", EditorInputCommand::ToolFill,
        ToolPanelKind::Smart, ToolCursor::Crosshair},
    {ActiveVoxelTool::Selection, "Select", EditorInputCommand::ToolSelection,
        ToolPanelKind::Selection, ToolCursor::Selection},
    {ActiveVoxelTool::Move, "Move", EditorInputCommand::ToolMove,
        ToolPanelKind::Move, ToolCursor::Move},
    {ActiveVoxelTool::Rotate, "Rotate", EditorInputCommand::ToolRotate,
        ToolPanelKind::Rotate, ToolCursor::Rotate},
    {ActiveVoxelTool::Scale, "Scale", EditorInputCommand::ToolScale,
        ToolPanelKind::Scale, ToolCursor::Scale},
    {ActiveVoxelTool::Wrap, "Wrap", EditorInputCommand::ToolWrap,
        ToolPanelKind::Wrap, ToolCursor::Scale},
    {ActiveVoxelTool::Box, "Box", EditorInputCommand::ToolBox,
        ToolPanelKind::Generic, ToolCursor::Crosshair},
    {ActiveVoxelTool::Line, "Line", EditorInputCommand::ToolLine,
        ToolPanelKind::Generic, ToolCursor::Crosshair},
    {ActiveVoxelTool::Sphere, "Sphere", EditorInputCommand::ToolSphere,
        ToolPanelKind::Generic, ToolCursor::Crosshair},
    {ActiveVoxelTool::Duplicate, "Duplicate", EditorInputCommand::ToolDuplicate,
        ToolPanelKind::Generic, ToolCursor::Move},
    {ActiveVoxelTool::Mirror, "Mirror", EditorInputCommand::ToolMirror,
        ToolPanelKind::Generic, ToolCursor::Default},
    {ActiveVoxelTool::Align, "Align", EditorInputCommand::ToolAlign,
        ToolPanelKind::Generic, ToolCursor::Default},
    {ActiveVoxelTool::None, "No Tool", EditorInputCommand::None,
        ToolPanelKind::Generic, ToolCursor::Default}
}};

// The toolbar is data-driven so persistence and user reordering can be added
// later without changing rendering or tool behavior.
constexpr std::array<ActiveVoxelTool, ToolManager::PrimaryToolCount>
    PrimaryOrder{{
        ActiveVoxelTool::Pencil,
        ActiveVoxelTool::Selection,
        ActiveVoxelTool::Move
    }};
}

ToolManager::ToolManager(VoxelToolState& state) noexcept : state_(state) {}

ActiveVoxelTool ToolManager::ActiveTool() const noexcept
{
    return state_.ActiveTool();
}

void ToolManager::SetActiveTool(const ActiveVoxelTool tool) noexcept
{
    state_.SetActiveTool(tool);
}

const ToolDescriptor& ToolManager::ActiveDescriptor() const noexcept
{
    return Descriptor(ActiveTool());
}

const ToolDescriptor& ToolManager::Descriptor(
    const ActiveVoxelTool tool) noexcept
{
    const auto found = std::find_if(
        ToolDefinitions.begin(), ToolDefinitions.end(),
        [tool](const ToolDescriptor& definition)
        {
            return definition.Tool == tool;
        });
    return found == ToolDefinitions.end() ? ToolDefinitions.back() : *found;
}

std::span<const ActiveVoxelTool, ToolManager::PrimaryToolCount>
ToolManager::PrimaryToolOrder() noexcept
{
    return PrimaryOrder;
}

std::span<const ToolDescriptor, ToolManager::ToolCount>
ToolManager::Tools() noexcept
{
    return ToolDefinitions;
}

} // namespace VoxelForge::Editor
