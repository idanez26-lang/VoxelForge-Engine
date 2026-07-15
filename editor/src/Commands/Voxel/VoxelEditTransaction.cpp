#include "VoxelEditTransaction.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <exception>
#include <string>

namespace VoxelForge::Editor
{
namespace
{

CommandResult ValidateSession(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    Voxel::VoxelModel*& model) noexcept
{
    if (session.VoxelModelGeneration() != modelGeneration)
        return CommandResult::Failure("The voxel model session has changed.");
    model = session.ActiveVoxelModel();
    if (model == nullptr)
        return CommandResult::Failure("No active voxel model is available.");
    if (model->GetGrid(0U) == nullptr)
        return CommandResult::Failure("The active voxel model has no grid.");
    return CommandResult::Success();
}

} // namespace

CommandResult ReadEditableVoxel(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    Voxel::Voxel& voxel) noexcept
{
    Voxel::VoxelModel* model = nullptr;
    CommandResult validation = ValidateSession(session, modelGeneration, model);
    if (!validation) return validation;

    const Voxel::Voxel* current = model->GetGrid(0U)->Get(x, y, z);
    if (current == nullptr)
        return CommandResult::Failure("Voxel coordinates are outside the active grid.");
    voxel = *current;
    return CommandResult::Success();
}

CommandResult ApplyVoxelEdit(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const Voxel::Voxel expected,
    const Voxel::Voxel replacement)
{
    Voxel::VoxelModel* model = nullptr;
    CommandResult validation = ValidateSession(session, modelGeneration, model);
    if (!validation) return validation;

    Voxel::VoxelGrid* grid = model->GetGrid(0U);
    const Voxel::Voxel* current = grid->Get(x, y, z);
    if (current == nullptr)
        return CommandResult::Failure("Voxel coordinates are outside the active grid.");
    if (*current != expected)
        return CommandResult::Failure("The active voxel no longer matches command history.");
    if (!grid->Set(x, y, z, replacement))
        return CommandResult::Failure("Unable to update the active voxel grid.");

    CommandResult rebuilt;
    try
    {
        rebuilt = session.RebuildActiveVoxelMesh();
    }
    catch (const std::exception& exception)
    {
        const bool rolledBack = grid->Set(x, y, z, expected);
        return CommandResult::Failure(
            std::string("Voxel mesh rebuild threw an exception: ") +
            exception.what() +
            (rolledBack ? "" : " CPU rollback also failed."));
    }
    catch (...)
    {
        const bool rolledBack = grid->Set(x, y, z, expected);
        return CommandResult::Failure(
            std::string("Voxel mesh rebuild threw an unknown exception.") +
            (rolledBack ? "" : " CPU rollback also failed."));
    }
    if (!rebuilt)
    {
        const bool rolledBack = grid->Set(x, y, z, expected);
        return CommandResult::Failure(
            rebuilt.Message + (rolledBack ? "" : " CPU rollback also failed."));
    }

    session.CompleteVoxelEdit();
    return CommandResult::Success();
}

} // namespace VoxelForge::Editor
