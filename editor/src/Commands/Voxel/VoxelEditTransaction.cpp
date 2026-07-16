#include "VoxelEditTransaction.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

CommandResult ApplyVoxelChanges(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::span<const VoxelChange> changes,
    const VoxelChangeDirection direction)
{
    if (changes.empty())
        return CommandResult::Failure("Voxel change set is empty.");
    if (session.VoxelModelGeneration() != modelGeneration)
        return CommandResult::Failure("The voxel model session has changed.");
    Voxel::VoxelModel* model = session.ActiveVoxelModel();
    Asset::Voxel::VoxelDocument* document = session.ActiveVoxelDocument();
    if (model == nullptr || document == nullptr)
        return CommandResult::Failure(
            "An editable voxel document and compatibility model are required.");

    struct GridSnapshot final
    {
        std::size_t ModelIndex = 0U;
        Voxel::VoxelGrid Grid;
    };
    std::vector<VoxelChange> directedChanges;
    std::vector<GridSnapshot> gridSnapshots;
    std::optional<Asset::Voxel::VoxelDocument> documentSnapshot;
    try
    {
        directedChanges.reserve(changes.size());
        gridSnapshots.reserve(model->GridCount());
        for (const VoxelChange& source : changes)
        {
            VoxelChange directed = source;
            if (direction == VoxelChangeDirection::Backward)
            {
                std::swap(directed.ExistedBefore, directed.ExistsAfter);
                std::swap(
                    directed.PaletteIndexBefore,
                    directed.PaletteIndexAfter);
            }
            if (directed.Position.X < 0 || directed.Position.Y < 0 ||
                directed.Position.Z < 0)
            {
                return CommandResult::Failure(
                    "Voxel change coordinates cannot be negative.");
            }
            Voxel::VoxelGrid* grid = model->GetGrid(directed.SubModelIndex);
            if (grid == nullptr)
                return CommandResult::Failure(
                    "Voxel change references an unavailable compatibility grid.");
            const auto x = static_cast<std::uint32_t>(directed.Position.X);
            const auto y = static_cast<std::uint32_t>(directed.Position.Y);
            const auto z = static_cast<std::uint32_t>(directed.Position.Z);
            const Voxel::Voxel* current = grid->Get(x, y, z);
            if (current == nullptr)
                return CommandResult::Failure(
                    "Voxel change coordinates lie outside the compatibility grid.");
            const bool gridMatches = directed.ExistedBefore
                ? current->IsOccupied() &&
                    current->ColorIndex == directed.PaletteIndexBefore
                : !current->IsOccupied();
            if (!gridMatches)
                return CommandResult::Failure(
                    "VoxelDocument and compatibility grid differ from the expected state.");
            directedChanges.push_back(directed);
        }
        documentSnapshot = *document;
        for (std::size_t index = 0U; index < model->GridCount(); ++index)
        {
            bool referenced = false;
            for (const VoxelChange& change : directedChanges)
            {
                if (change.SubModelIndex == index)
                {
                    referenced = true;
                    break;
                }
            }
            if (referenced)
                gridSnapshots.push_back({index, *model->GetGrid(index)});
        }
    }
    catch (const std::exception& exception)
    {
        return CommandResult::Failure(
            std::string("Unable to start atomic voxel history edit: ") +
            exception.what());
    }
    catch (...)
    {
        return CommandResult::Failure(
            "Unable to start atomic voxel history edit.");
    }

    const auto rollback = [&]() noexcept
    {
        if (documentSnapshot) *document = std::move(*documentSnapshot);
        for (GridSnapshot& snapshot : gridSnapshots)
        {
            if (Voxel::VoxelGrid* grid = model->GetGrid(snapshot.ModelIndex))
                *grid = std::move(snapshot.Grid);
        }
    };

    try
    {
        const Asset::Voxel::VoxelDocumentOperationResult documentResult =
            document->ApplyVoxelChanges(directedChanges);
        if (!documentResult.Succeeded || !documentResult.Changed)
        {
            rollback();
            return CommandResult::Failure(
                "VoxelDocument history edit failed: " + documentResult.Message);
        }
        for (const VoxelChange& change : directedChanges)
        {
            Voxel::VoxelGrid* grid = model->GetGrid(change.SubModelIndex);
            const Voxel::Voxel replacement = change.ExistsAfter
                ? Voxel::Voxel{
                    change.PaletteIndexAfter, Voxel::Voxel::OccupiedFlag}
                : Voxel::Voxel{};
            if (!grid->Set(
                    static_cast<std::uint32_t>(change.Position.X),
                    static_cast<std::uint32_t>(change.Position.Y),
                    static_cast<std::uint32_t>(change.Position.Z),
                    replacement))
            {
                rollback();
                return CommandResult::Failure(
                    "Unable to update a compatibility grid voxel.");
            }
        }
    }
    catch (const std::exception& exception)
    {
        rollback();
        return CommandResult::Failure(
            std::string("Atomic voxel history edit threw an exception: ") +
            exception.what());
    }
    catch (...)
    {
        rollback();
        return CommandResult::Failure(
            "Atomic voxel history edit threw an unknown exception.");
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
