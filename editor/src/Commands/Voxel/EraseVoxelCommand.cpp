#include "EraseVoxelCommand.h"
#include "VoxelEditTransaction.h"

namespace VoxelForge::Editor
{

EraseVoxelCommand::EraseVoxelCommand(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) noexcept
    : session_(session),
      modelGeneration_(modelGeneration),
      x_(x),
      y_(y),
      z_(z)
{
}

CommandResult EraseVoxelCommand::Execute()
{
    Voxel::Voxel voxel;
    CommandResult validation = ReadEditableVoxel(
        session_, modelGeneration_, x_, y_, z_, voxel);
    if (!validation) return validation;
    if (!voxel.IsOccupied())
        return CommandResult::Failure("The selected voxel is already empty.");

    const CommandResult result = Apply(voxel, Voxel::Voxel{});
    if (result)
    {
        previousVoxel_ = voxel;
        previousVoxelCaptured_ = true;
    }
    return result;
}

CommandResult EraseVoxelCommand::Undo()
{
    if (!previousVoxelCaptured_)
        return CommandResult::Failure("Erase command has not executed successfully.");
    return Apply(Voxel::Voxel{}, previousVoxel_);
}

CommandResult EraseVoxelCommand::Redo()
{
    if (!previousVoxelCaptured_)
        return CommandResult::Failure("Erase command has not executed successfully.");
    return Apply(previousVoxel_, Voxel::Voxel{});
}

std::string_view EraseVoxelCommand::Name() const noexcept
{
    return "Erase Voxel";
}

const Voxel::Voxel& EraseVoxelCommand::PreviousVoxel() const noexcept
{
    return previousVoxel_;
}

CommandResult EraseVoxelCommand::Apply(
    const Voxel::Voxel expected,
    const Voxel::Voxel replacement)
{
    return ApplyVoxelEdit(
        session_, modelGeneration_, x_, y_, z_, expected, replacement);
}

} // namespace VoxelForge::Editor
