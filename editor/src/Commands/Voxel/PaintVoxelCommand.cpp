#include "PaintVoxelCommand.h"
#include "VoxelEditTransaction.h"

#include "VoxelForge/Voxel/VoxelPalette.h"

namespace VoxelForge::Editor
{

PaintVoxelCommand::PaintVoxelCommand(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const std::size_t newColorIndex) noexcept
    : session_(session),
      modelGeneration_(modelGeneration),
      x_(x),
      y_(y),
      z_(z),
      newColorIndex_(newColorIndex)
{
}

CommandResult PaintVoxelCommand::Execute()
{
    if (newColorIndex_ >= Voxel::VoxelPalette::Size())
        return CommandResult::Failure("Paint color index is outside the active palette.");

    Voxel::Voxel voxel;
    CommandResult validation = ReadEditableVoxel(
        session_, modelGeneration_, x_, y_, z_, voxel);
    if (!validation) return validation;
    if (!voxel.IsOccupied())
        return CommandResult::Failure("The selected voxel is empty.");
    if (voxel.ColorIndex == newColorIndex_)
        return CommandResult::Failure("The selected voxel already uses this color.");

    Voxel::Voxel painted = voxel;
    painted.ColorIndex = static_cast<std::uint8_t>(newColorIndex_);
    const CommandResult result = Apply(voxel, painted);
    if (result)
    {
        previousVoxel_ = voxel;
        paintedVoxel_ = painted;
        previousVoxelCaptured_ = true;
    }
    return result;
}

CommandResult PaintVoxelCommand::Undo()
{
    if (!previousVoxelCaptured_)
        return CommandResult::Failure("Paint command has not executed successfully.");
    return Apply(paintedVoxel_, previousVoxel_);
}

CommandResult PaintVoxelCommand::Redo()
{
    if (!previousVoxelCaptured_)
        return CommandResult::Failure("Paint command has not executed successfully.");
    return Apply(previousVoxel_, paintedVoxel_);
}

std::string_view PaintVoxelCommand::Name() const noexcept
{
    return "Paint Voxel";
}

const Voxel::Voxel& PaintVoxelCommand::PreviousVoxel() const noexcept
{
    return previousVoxel_;
}

const Voxel::Voxel& PaintVoxelCommand::PaintedVoxel() const noexcept
{
    return paintedVoxel_;
}

CommandResult PaintVoxelCommand::Apply(
    const Voxel::Voxel expected,
    const Voxel::Voxel replacement)
{
    return ApplyVoxelEdit(
        session_, modelGeneration_, x_, y_, z_, expected, replacement);
}

} // namespace VoxelForge::Editor
