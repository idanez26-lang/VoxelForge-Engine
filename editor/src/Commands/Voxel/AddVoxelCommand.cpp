#include "AddVoxelCommand.h"
#include "VoxelEditTransaction.h"

#include "VoxelForge/Voxel/VoxelPalette.h"

namespace VoxelForge::Editor
{

AddVoxelCommand::AddVoxelCommand(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const std::size_t colorIndex) noexcept
    : session_(session),
      modelGeneration_(modelGeneration),
      x_(x),
      y_(y),
      z_(z),
      colorIndex_(colorIndex)
{
}

CommandResult AddVoxelCommand::Execute()
{
    // VF-STAB-01 bugs 6-7: no lower-bound check here on purpose. This command
    // only ever runs on the raw VoxelGrid path (no active document), where
    // occupancy is a separate flag and palette index 0 is a legitimate colour —
    // covered by AddVoxelTests. The index-0 defect lives in the document path
    // and is fixed at its source, PaintPaletteSelection.
    if (colorIndex_ >= Voxel::VoxelPalette::Size())
    {
        return CommandResult::Failure(
            "Add color index is outside the active palette.");
    }

    Voxel::Voxel voxel;
    CommandResult validation = ReadEditableVoxel(
        session_, modelGeneration_, x_, y_, z_, voxel);
    if (!validation)
    {
        return validation;
    }
    if (voxel.IsOccupied())
    {
        return CommandResult::Failure(
            "The destination voxel is already occupied.");
    }

    const Voxel::Voxel added{
        static_cast<std::uint8_t>(colorIndex_),
        Voxel::Voxel::OccupiedFlag};
    const CommandResult result = Apply(voxel, added);
    if (result)
    {
        previousVoxel_ = voxel;
        addedVoxel_ = added;
        previousVoxelCaptured_ = true;
    }
    return result;
}

CommandResult AddVoxelCommand::Undo()
{
    if (!previousVoxelCaptured_)
    {
        return CommandResult::Failure(
            "Add command has not executed successfully.");
    }
    return Apply(addedVoxel_, previousVoxel_);
}

CommandResult AddVoxelCommand::Redo()
{
    if (!previousVoxelCaptured_)
    {
        return CommandResult::Failure(
            "Add command has not executed successfully.");
    }
    return Apply(previousVoxel_, addedVoxel_);
}

std::string_view AddVoxelCommand::Name() const noexcept
{
    return "Add Voxel";
}

const Voxel::Voxel& AddVoxelCommand::PreviousVoxel() const noexcept
{
    return previousVoxel_;
}

const Voxel::Voxel& AddVoxelCommand::AddedVoxel() const noexcept
{
    return addedVoxel_;
}

CommandResult AddVoxelCommand::Apply(
    const Voxel::Voxel expected,
    const Voxel::Voxel replacement)
{
    return ApplyVoxelEdit(
        session_, modelGeneration_, x_, y_, z_, expected, replacement);
}

} // namespace VoxelForge::Editor
