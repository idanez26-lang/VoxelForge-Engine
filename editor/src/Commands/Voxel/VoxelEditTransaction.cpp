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

struct EditableSessionState final
{
    Voxel::VoxelModel* Model = nullptr;
    Voxel::VoxelGrid* Grid = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    const Asset::Voxel::VoxelSubModel* DocumentModel = nullptr;
};

CommandResult ValidateSession(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const std::size_t modelIndex,
    EditableSessionState& state) noexcept
{
    if (session.VoxelModelGeneration() != modelGeneration)
        return CommandResult::Failure("The voxel model session has changed.");

    state.Document = session.ActiveVoxelDocument();
    state.Model = session.ActiveVoxelModel();
    if (state.Document == nullptr && state.Model == nullptr)
        return CommandResult::Failure(
            "No active voxel document or compatibility model is available.");

    if (state.Document != nullptr)
    {
        state.DocumentModel = state.Document->GetModel(modelIndex);
        if (state.DocumentModel == nullptr)
            return CommandResult::Failure(
                "The active VoxelDocument has no matching sub-model.");
    }
    if (state.Model != nullptr)
    {
        state.Grid = state.Model->GetGrid(modelIndex);
        if (state.Grid == nullptr)
            return CommandResult::Failure(
                "The compatibility model has no matching grid.");
    }
    return CommandResult::Success();
}

CommandResult ReadDocumentVoxel(
    const EditableSessionState& state,
    const std::size_t modelIndex,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    Voxel::Voxel& voxel) noexcept
{
    if (state.Document == nullptr || state.DocumentModel == nullptr)
        return CommandResult::Failure("No active VoxelDocument is available.");

    const Asset::Voxel::VoxelDimensions dimensions =
        state.DocumentModel->Dimensions();
    if (x >= dimensions.X || y >= dimensions.Y || z >= dimensions.Z)
        return CommandResult::Failure(
            "Voxel coordinates are outside the active document.");

    const std::optional<Asset::Voxel::Voxel> current =
        state.Document->GetVoxel({
            static_cast<std::int32_t>(x),
            static_cast<std::int32_t>(y),
            static_cast<std::int32_t>(z)}, modelIndex);
    voxel = current
        ? Voxel::Voxel{
            current->PaletteIndex, Voxel::Voxel::OccupiedFlag}
        : Voxel::Voxel{};
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
    EditableSessionState state;
    CommandResult validation = ValidateSession(
        session, modelGeneration, 0U, state);
    if (!validation) return validation;

    if (state.Document != nullptr)
    {
        validation = ReadDocumentVoxel(state, 0U, x, y, z, voxel);
        if (!validation) return validation;
        if (state.Grid != nullptr)
        {
            const Voxel::Voxel* compatibility = state.Grid->Get(x, y, z);
            if (compatibility == nullptr || *compatibility != voxel)
                return CommandResult::Failure(
                    "VoxelDocument and compatibility grid have diverged.");
        }
        return CommandResult::Success();
    }

    const Voxel::Voxel* current = state.Grid->Get(x, y, z);
    if (current == nullptr)
        return CommandResult::Failure(
            "Voxel coordinates are outside the active grid.");
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
    EditableSessionState state;
    CommandResult validation = ValidateSession(
        session, modelGeneration, modelIndex, state);
    if (!validation) return validation;

    Voxel::Voxel current;
    if (state.Document != nullptr)
    {
        validation = ReadDocumentVoxel(
            state, modelIndex, x, y, z, current);
        if (!validation) return validation;
    }
    else
    {
        const Voxel::Voxel* compatibility = state.Grid->Get(x, y, z);
        if (compatibility == nullptr)
            return CommandResult::Failure(
                "Voxel coordinates are outside the active grid.");
        current = *compatibility;
    }
    if (current != expected)
        return CommandResult::Failure("The active voxel no longer matches command history.");

    if (state.Grid != nullptr)
    {
        const Voxel::Voxel* compatibility = state.Grid->Get(x, y, z);
        if (compatibility == nullptr || *compatibility != expected)
            return CommandResult::Failure(
                "VoxelDocument and compatibility grid have diverged.");
    }

    const Asset::Voxel::VoxelPosition documentPosition{
        static_cast<std::int32_t>(x),
        static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z)};
    const auto applyDocumentVoxel = [
        document = state.Document, &documentPosition, modelIndex](
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
        if (state.Document != nullptr) documentSnapshot = *state.Document;
        if (state.Grid != nullptr) gridSnapshot = *state.Grid;
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
        if (state.Grid != nullptr && gridSnapshot)
            *state.Grid = std::move(*gridSnapshot);
        if (state.Document != nullptr && documentSnapshot)
            *state.Document = std::move(*documentSnapshot);
    };

    if (state.Document != nullptr)
    {
        const Asset::Voxel::VoxelDocumentOperationResult changed =
            applyDocumentVoxel(replacement);
        if (!changed.Succeeded)
        {
            rollback();
            return CommandResult::Failure(
                "VoxelDocument edit failed: " + changed.Message);
        }
    }
    if (state.Grid != nullptr && !state.Grid->Set(x, y, z, replacement))
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
    try
    {
        VoxelEditOperation operation;
        operation.Changes.assign(changes.begin(), changes.end());
        return ApplyVoxelEditOperation(
            session, modelGeneration, operation, direction);
    }
    catch (const std::exception& exception)
    {
        return CommandResult::Failure(
            std::string("Unable to prepare atomic voxel history edit: ") +
            exception.what());
    }
    catch (...)
    {
        return CommandResult::Failure(
            "Unable to prepare atomic voxel history edit.");
    }
}

CommandResult ApplyVoxelEditOperation(
    VoxelEditSession& session,
    const std::uint64_t modelGeneration,
    const VoxelEditOperation& operation,
    const VoxelChangeDirection direction)
{
    if (operation.Changes.empty() && !operation.PaletteChange)
        return CommandResult::Failure("Voxel edit operation is empty.");
    if (session.VoxelModelGeneration() != modelGeneration)
        return CommandResult::Failure("The voxel model session has changed.");
    Voxel::VoxelModel* model = session.ActiveVoxelModel();
    Asset::Voxel::VoxelDocument* document = session.ActiveVoxelDocument();
    if (document == nullptr)
        return CommandResult::Failure(
            "An editable VoxelDocument is required.");

    struct GridSnapshot final
    {
        std::size_t ModelIndex = 0U;
        Voxel::VoxelGrid Grid;
    };
    std::vector<VoxelChange> directedChanges;
    std::vector<GridSnapshot> gridSnapshots;
    std::optional<Asset::Voxel::VoxelDocument> documentSnapshot;
    std::optional<Voxel::VoxelPalette> paletteSnapshot;
    std::optional<VoxelPaletteChange> directedPaletteChange;
    try
    {
        directedChanges.reserve(operation.Changes.size());
        if (model != nullptr)
            gridSnapshots.reserve(model->GridCount());
        for (const VoxelChange& source : operation.Changes)
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
            const Asset::Voxel::VoxelSubModel* documentModel =
                document->GetModel(directed.SubModelIndex);
            if (documentModel == nullptr)
                return CommandResult::Failure(
                    "Voxel change references an unavailable document sub-model.");
            const auto x = static_cast<std::uint32_t>(directed.Position.X);
            const auto y = static_cast<std::uint32_t>(directed.Position.Y);
            const auto z = static_cast<std::uint32_t>(directed.Position.Z);
            const Asset::Voxel::VoxelDimensions dimensions =
                documentModel->Dimensions();
            if (x >= dimensions.X || y >= dimensions.Y || z >= dimensions.Z)
                return CommandResult::Failure(
                    "Voxel change coordinates lie outside the document sub-model.");
            const std::optional<Asset::Voxel::Voxel> documentVoxel =
                document->GetVoxel(
                    directed.Position, directed.SubModelIndex);
            const bool documentMatches = directed.ExistedBefore
                ? documentVoxel &&
                    documentVoxel->PaletteIndex == directed.PaletteIndexBefore
                : !documentVoxel;
            if (!documentMatches)
                return CommandResult::Failure(
                    "VoxelDocument differs from the expected state.");
            if (model != nullptr)
            {
                Voxel::VoxelGrid* grid =
                    model->GetGrid(directed.SubModelIndex);
                if (grid == nullptr)
                    return CommandResult::Failure(
                        "Voxel change references an unavailable compatibility grid.");
                const Voxel::Voxel* current = grid->Get(x, y, z);
                const bool gridMatches = current != nullptr &&
                    (directed.ExistedBefore
                        ? current->IsOccupied() &&
                            current->ColorIndex == directed.PaletteIndexBefore
                        : !current->IsOccupied());
                if (!gridMatches)
                    return CommandResult::Failure(
                        "The compatibility grid differs from the VoxelDocument state.");
            }
            directedChanges.push_back(directed);
        }
        if (operation.PaletteChange)
        {
            directedPaletteChange = *operation.PaletteChange;
            if (direction == VoxelChangeDirection::Backward)
                std::swap(
                    directedPaletteChange->Before,
                    directedPaletteChange->After);
            if (document->GetPaletteSnapshot() !=
                directedPaletteChange->Before)
            {
                return CommandResult::Failure(
                    "The VoxelDocument palette no longer matches the expected state.");
            }
            for (std::size_t index = 0U;
                 model != nullptr && index < Voxel::VoxelPalette::Size(); ++index)
            {
                const Asset::Voxel::VoxelColor& expected =
                    directedPaletteChange->Before.Colors[index];
                const Voxel::VoxelColor* current =
                    model->Palette().Get(index);
                if (current == nullptr || *current != Voxel::VoxelColor{
                        expected.Red,
                        expected.Green,
                        expected.Blue,
                        expected.Alpha})
                {
                    return CommandResult::Failure(
                        "The compatibility model palette no longer matches the expected state.");
                }
            }
        }
        documentSnapshot = *document;
        if (model != nullptr) paletteSnapshot = model->Palette();
        for (std::size_t index = 0U;
             model != nullptr && index < model->GridCount(); ++index)
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
        if (model != nullptr && paletteSnapshot)
            model->Palette() = std::move(*paletteSnapshot);
        for (GridSnapshot& snapshot : gridSnapshots)
        {
            if (model != nullptr)
            {
                Voxel::VoxelGrid* grid =
                    model->GetGrid(snapshot.ModelIndex);
                if (grid != nullptr)
                    *grid = std::move(snapshot.Grid);
            }
        }
    };

    try
    {
        const Asset::Voxel::VoxelDocumentCompositeOrder compositeOrder =
            direction == VoxelChangeDirection::Forward
            ? Asset::Voxel::VoxelDocumentCompositeOrder::PaletteThenVoxels
            : Asset::Voxel::VoxelDocumentCompositeOrder::VoxelsThenPalette;
        const Asset::Voxel::VoxelDocumentOperationResult documentResult =
            document->ApplyCompositeChanges(
                directedChanges,
                directedPaletteChange ? &*directedPaletteChange : nullptr,
                compositeOrder);
        if (!documentResult.Succeeded || !documentResult.Changed)
        {
            rollback();
            return CommandResult::Failure(
                "VoxelDocument history edit failed: " + documentResult.Message);
        }
        const auto synchronizeModelPalette = [&]() noexcept
        {
            if (model == nullptr || !directedPaletteChange) return true;
            for (std::size_t index = 0U;
                 index < Voxel::VoxelPalette::Size(); ++index)
            {
                const Asset::Voxel::VoxelColor& source =
                    directedPaletteChange->After.Colors[index];
                if (!model->Palette().Set(index, Voxel::VoxelColor{
                        source.Red, source.Green, source.Blue, source.Alpha}))
                    return false;
            }
            return true;
        };
        if (direction == VoxelChangeDirection::Forward &&
            !synchronizeModelPalette())
        {
            rollback();
            return CommandResult::Failure(
                "Unable to synchronize the compatibility model palette.");
        }
        for (const VoxelChange& change : directedChanges)
        {
            if (model == nullptr) break;
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
        if (direction == VoxelChangeDirection::Backward &&
            !synchronizeModelPalette())
        {
            rollback();
            return CommandResult::Failure(
                "Unable to synchronize the compatibility model palette.");
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
