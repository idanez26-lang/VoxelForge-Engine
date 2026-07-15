#include "EraseVoxelCommand.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <exception>
#include <string>

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
    Voxel::VoxelModel* model = nullptr;
    CommandResult validation = ValidateSession(model);
    if (!validation) return validation;

    Voxel::VoxelGrid* grid = model->GetGrid(0U);
    const Voxel::Voxel* voxel = grid->Get(x_, y_, z_);
    if (voxel == nullptr)
        return CommandResult::Failure("Voxel coordinates are outside the active grid.");
    if (!voxel->IsOccupied())
        return CommandResult::Failure("The selected voxel is already empty.");

    previousVoxel_ = *voxel;
    previousVoxelCaptured_ = true;
    return Apply(previousVoxel_, Voxel::Voxel{});
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
    Voxel::VoxelModel* model = nullptr;
    CommandResult validation = ValidateSession(model);
    if (!validation) return validation;

    Voxel::VoxelGrid* grid = model->GetGrid(0U);
    const Voxel::Voxel* current = grid->Get(x_, y_, z_);
    if (current == nullptr)
        return CommandResult::Failure("Voxel coordinates are outside the active grid.");
    if (*current != expected)
        return CommandResult::Failure("The active voxel no longer matches command history.");
    if (!grid->Set(x_, y_, z_, replacement))
        return CommandResult::Failure("Unable to update the active voxel grid.");

    CommandResult rebuilt;
    try
    {
        rebuilt = session_.RebuildActiveVoxelMesh();
    }
    catch (const std::exception& exception)
    {
        const bool rolledBack = grid->Set(x_, y_, z_, expected);
        return CommandResult::Failure(
            std::string("Voxel mesh rebuild threw an exception: ") +
            exception.what() +
            (rolledBack ? "" : " CPU rollback also failed."));
    }
    catch (...)
    {
        const bool rolledBack = grid->Set(x_, y_, z_, expected);
        return CommandResult::Failure(
            std::string("Voxel mesh rebuild threw an unknown exception.") +
            (rolledBack ? "" : " CPU rollback also failed."));
    }
    if (!rebuilt)
    {
        const bool rolledBack = grid->Set(x_, y_, z_, expected);
        return CommandResult::Failure(
            rebuilt.Message + (rolledBack ? "" : " CPU rollback also failed."));
    }

    session_.CompleteVoxelEdit();
    return CommandResult::Success();
}

CommandResult EraseVoxelCommand::ValidateSession(
    Voxel::VoxelModel*& model) const noexcept
{
    if (session_.VoxelModelGeneration() != modelGeneration_)
        return CommandResult::Failure("The voxel model session has changed.");
    model = session_.ActiveVoxelModel();
    if (model == nullptr)
        return CommandResult::Failure("No active voxel model is available.");
    if (model->GetGrid(0U) == nullptr)
        return CommandResult::Failure("The active voxel model has no grid.");
    return CommandResult::Success();
}

} // namespace VoxelForge::Editor
