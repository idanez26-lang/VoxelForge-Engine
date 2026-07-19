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
    case ActiveVoxelTool::Fill: return "Fill";
    case ActiveVoxelTool::Box: return "Box";
    case ActiveVoxelTool::Line: return "Line";
    case ActiveVoxelTool::Sphere: return "Sphere";
    case ActiveVoxelTool::Selection: return "Selection";
    case ActiveVoxelTool::Move: return "Move";
    case ActiveVoxelTool::Duplicate: return "Duplicate";
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

bool VoxelToolState::IsFillActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Fill;
}

bool VoxelToolState::IsBoxActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Box;
}

bool VoxelToolState::IsLineActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Line;
}

bool VoxelToolState::IsSphereActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Sphere;
}

bool VoxelToolState::IsSelectionActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Selection;
}

bool VoxelToolState::IsMoveActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Move;
}

bool VoxelToolState::IsDuplicateActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Duplicate;
}

bool VoxelToolState::IsEditingToolActive() const noexcept
{
    return activeTool_ == ActiveVoxelTool::Pencil ||
        activeTool_ == ActiveVoxelTool::Eraser ||
        activeTool_ == ActiveVoxelTool::Fill ||
        activeTool_ == ActiveVoxelTool::Box ||
        activeTool_ == ActiveVoxelTool::Line ||
        activeTool_ == ActiveVoxelTool::Sphere;
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
