#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"

#include "Commands/Voxel/VoxelEditSession.h"

#include <memory>
#include <new>
#include <string>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{
VoxelEditHistoryResult ExecutionResult(
    const VoxelEditHistoryResultCode code,
    std::string message)
{
    return {
        .Code = code,
        .Changed = false,
        .Label = "Place Voxel Stamp",
        .Message = std::move(message)};
}
}

PlaceVoxelStampPreparation PreparePlaceVoxelStampOperation(
    const StampPlacementPlan& plan) noexcept
{
    if (plan.DocumentGeneration == 0U ||
        plan.Document.InstanceToken == 0U)
    {
        return {
            .Status = PlaceVoxelStampPreparationStatus::InvalidInput,
            .PaletteStatus = plan.PaletteStatus};
    }
    if (!plan.CanCommit)
    {
        if (!plan.HasErrors() &&
            plan.Statistics.ChangedVoxelCount == 0U &&
            !plan.PaletteMapping.HasPaletteChanges())
        {
            return {
                .Status = PlaceVoxelStampPreparationStatus::NoChange,
                .PaletteStatus = plan.PaletteStatus};
        }
        return {
            .Status = plan.PaletteStatus == PaletteMappingStatus::Success ||
                    plan.PaletteStatus == PaletteMappingStatus::NoChange
                ? PlaceVoxelStampPreparationStatus::InvalidPreview
                : PlaceVoxelStampPreparationStatus::PaletteMappingFailed,
            .PaletteStatus = plan.PaletteStatus};
    }

    try
    {
        VoxelEditOperation operation;
        operation.Label = "Place Voxel Stamp";
        operation.Changes.reserve(plan.Statistics.ChangedVoxelCount);
        for (const StampPlannedVoxel& voxel : plan.Voxels)
        {
            if (voxel.OutOfBounds || voxel.Skipped ||
                (voxel.ExistingVoxel &&
                    voxel.ExistingVoxel->PaletteIndex ==
                        voxel.FinalVoxel.PaletteIndex))
            {
                continue;
            }
            operation.Changes.push_back({
                .SubModelIndex = plan.TargetSubModel,
                .Position = voxel.WorldPosition,
                .ExistedBefore = voxel.ExistingVoxel.has_value(),
                .PaletteIndexBefore = voxel.ExistingVoxel
                    ? voxel.ExistingVoxel->PaletteIndex
                    : 0U,
                .ExistsAfter = true,
                .PaletteIndexAfter = voxel.FinalVoxel.PaletteIndex});
        }
        if (plan.PaletteMapping.HasPaletteChanges())
        {
            operation.PaletteChange = std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
                Asset::Voxel::VoxelDocumentPaletteChange{
                    .Before = plan.DocumentPaletteBefore,
                    .After = plan.PaletteMapping.FinalDocumentPalette});
        }
        if (operation.Changes.empty() && !operation.PaletteChange)
        {
            return {.Status = PlaceVoxelStampPreparationStatus::NoChange,
                    .PaletteStatus = plan.PaletteStatus};
        }

        return {.Status = PlaceVoxelStampPreparationStatus::Ready,
                .PaletteStatus = plan.PaletteStatus,
                .Operation = std::move(operation)};
    }
    catch (const std::bad_alloc&)
    {
        return {.Status = PlaceVoxelStampPreparationStatus::AllocationFailure};
    }
}

VoxelEditHistoryResult ExecutePlaceVoxelStampOperation(
    const StampPlacementPlan& plan,
    VoxelEditSession& session,
    VoxelEditHistory& history)
{
    Asset::Voxel::VoxelDocument* const document =
        session.ActiveVoxelDocument();
    const std::uint64_t generation = session.VoxelModelGeneration();
    if (document == nullptr)
    {
        return ExecutionResult(
            VoxelEditHistoryResultCode::InvalidOperation,
            "Stamp placement requires an active VoxelDocument.");
    }
    if (!plan.IsCurrent(*document, generation, plan.TargetSubModel))
    {
        return ExecutionResult(
            VoxelEditHistoryResultCode::InvalidOperation,
            "The Stamp placement plan no longer matches the active document, generation or revision.");
    }

    PlaceVoxelStampPreparation prepared =
        PreparePlaceVoxelStampOperation(plan);
    if (prepared.IsNoChange())
    {
        return ExecutionResult(
            VoxelEditHistoryResultCode::NoChange,
            std::string(PlaceVoxelStampPreparationStatusMessage(
                prepared.Status)));
    }
    if (!prepared.IsReady())
    {
        std::string message =
            std::string(PlaceVoxelStampPreparationStatusMessage(
                prepared.Status));
        if (prepared.Status ==
            PlaceVoxelStampPreparationStatus::PaletteMappingFailed)
        {
            message += " ";
            message += PaletteMappingStatusMessage(
                prepared.PaletteStatus);
        }
        return ExecutionResult(
            prepared.Status ==
                    PlaceVoxelStampPreparationStatus::AllocationFailure
                ? VoxelEditHistoryResultCode::Failed
                : VoxelEditHistoryResultCode::InvalidOperation,
            std::move(message));
    }

    return history.Execute(session, std::move(prepared.Operation));
}

} // namespace VoxelForge::Editor::Stamps
