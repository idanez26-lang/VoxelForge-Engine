#include "VoxelEditTransaction.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{

CommandResult ValidateSession(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::size_t modelIndex,
    Voxel::VoxelModel*& model) noexcept
{
    if (session.VoxelModelGeneration() != modelGeneration)
        return CommandResult::Failure("The voxel model session has changed.");
    model = session.ActiveVoxelModel();
    if (model == nullptr)
        return CommandResult::Failure("No active voxel model is available.");
    if (model->GetGrid(modelIndex) == nullptr)
        return CommandResult::Failure(
            "The active voxel model has no matching grid.");
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
    CommandResult validation = ValidateSession(
        session, modelGeneration, 0U, model);
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
    const Voxel::Voxel replacement,
    const std::size_t modelIndex)
{
    Voxel::VoxelModel* model = nullptr;
    CommandResult validation = ValidateSession(
        session, modelGeneration, modelIndex, model);
    if (!validation) return validation;

    Voxel::VoxelGrid* grid = model->GetGrid(modelIndex);
    const Voxel::Voxel* current = grid->Get(x, y, z);
    if (current == nullptr)
        return CommandResult::Failure("Voxel coordinates are outside the active grid.");
    if (*current != expected)
        return CommandResult::Failure("The active voxel no longer matches command history.");

    Asset::Voxel::VoxelDocument* document = session.ActiveVoxelDocument();
    if (document != nullptr && document->GetModel(modelIndex) == nullptr)
        return CommandResult::Failure(
            "The active VoxelDocument has no matching sub-model.");
    const Asset::Voxel::VoxelPosition documentPosition{
        static_cast<std::int32_t>(x),
        static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z)};
    const auto applyDocumentVoxel = [
        document, &documentPosition, modelIndex](
        const Voxel::Voxel value)
    {
        return value.IsOccupied()
            ? document->SetVoxel(
                documentPosition, value.ColorIndex, modelIndex)
            : document->RemoveVoxel(documentPosition, modelIndex);
    };

    std::optional<Asset::Voxel::VoxelDocument> documentSnapshot;
    std::optional<Voxel::VoxelGrid> gridSnapshot;
    try
    {
        if (document != nullptr) documentSnapshot = *document;
        gridSnapshot = *grid;
    }
    catch (const std::exception& exception)
    {
        return CommandResult::Failure(
            std::string("Unable to start atomic voxel edit: ") +
            exception.what());
    }
    catch (...)
    {
        return CommandResult::Failure(
            "Unable to start atomic voxel edit.");
    }
    const auto rollback = [&]() noexcept
    {
        if (gridSnapshot) *grid = std::move(*gridSnapshot);
        if (document != nullptr && documentSnapshot)
            *document = std::move(*documentSnapshot);
    };

    if (document != nullptr)
    {
        const std::optional<Asset::Voxel::Voxel> documentVoxel =
            document->GetVoxel(documentPosition, modelIndex);
        const bool documentMatches = expected.IsOccupied()
            ? documentVoxel &&
                documentVoxel->PaletteIndex == expected.ColorIndex
            : !documentVoxel;
        if (!documentMatches)
            return CommandResult::Failure(
                "VoxelDocument and the editable compatibility grid diverged.");
        const Asset::Voxel::VoxelDocumentOperationResult changed =
            applyDocumentVoxel(replacement);
        if (!changed.Succeeded)
            return CommandResult::Failure(
                "VoxelDocument edit failed: " + changed.Message);
    }
    if (!grid->Set(x, y, z, replacement))
    {
        rollback();
        return CommandResult::Failure("Unable to update the active voxel grid.");
    }

    CommandResult rebuilt;
    try
    {
        rebuilt = session.RebuildActiveVoxelMesh();
    }
    catch (const std::exception& exception)
    {
        rollback();
        return CommandResult::Failure(
            std::string("Voxel mesh rebuild threw an exception: ") +
            exception.what());
    }
    catch (...)
    {
        rollback();
        return CommandResult::Failure(
            "Voxel mesh rebuild threw an unknown exception.");
    }
    if (!rebuilt)
    {
        rollback();
        return CommandResult::Failure(rebuilt.Message);
    }

    session.CompleteVoxelEdit();
    return CommandResult::Success();
}

} // namespace VoxelForge::Editor
