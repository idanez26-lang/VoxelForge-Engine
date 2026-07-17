#include "VoxelToolState.h"

namespace VoxelForge::Editor
{

const char* ActiveVoxelToolName(const ActiveVoxelTool tool) noexcept
{
    switch (tool)
    {
    case ActiveVoxelTool::None: return "None";
    case ActiveVoxelTool::Pencil: return "Pencil";
    case ActiveVoxelTool::Eraser: return "Eraser";
    }
    return "None";
}

void VoxelToolState::SetActiveTool(const ActiveVoxelTool tool) noexcept
{
    activeTool_ = tool;
}

void VoxelToolState::Reset() noexcept
{
    activeTool_ = ActiveVoxelTool::Pencil;
}

ActiveVoxelTool VoxelToolState::ActiveTool() const noexcept
{
    return activeTool_;
}

bool VoxelToolState::IsPencilActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Pencil;
}

bool VoxelToolState::IsEraserActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Eraser;
}

bool VoxelToolState::IsEditingToolActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Pencil ||
        activeTool_ == ActiveVoxelTool::Eraser;
}

ActiveVoxelTool ResolveVoxelToolShortcut(
    const ActiveVoxelTool current,
    const bool pencilPressed,
    const bool eraserPressed,
    const bool inputAllowed) noexcept
{
    if (!inputAllowed) return current;
    if (eraserPressed)
        return current == ActiveVoxelTool::Eraser
            ? ActiveVoxelTool::None : ActiveVoxelTool::Eraser;
    if (pencilPressed)
        return current == ActiveVoxelTool::Pencil
            ? ActiveVoxelTool::None : ActiveVoxelTool::Pencil;
    return current;
}

} // namespace VoxelForge::Editor
