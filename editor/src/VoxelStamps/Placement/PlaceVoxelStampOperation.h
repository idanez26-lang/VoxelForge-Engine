#pragma once

#include "VoxelHistory/VoxelEditOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/StampPlacementPlan.h"

#include <string_view>

namespace VoxelForge::Editor::Stamps
{

enum class PlaceVoxelStampPreparationStatus
{
    Ready,
    NoChange,
    InvalidInput,
    InvalidPreview,
    InvalidPreviewPosition,
    PaletteMappingFailed,
    AllocationFailure
};

[[nodiscard]] constexpr std::string_view PlaceVoxelStampPreparationStatusMessage(
    const PlaceVoxelStampPreparationStatus status) noexcept
{
    switch (status)
    {
    case PlaceVoxelStampPreparationStatus::Ready:
        return "Stamp placement operation is ready.";
    case PlaceVoxelStampPreparationStatus::NoChange:
        return "Stamp placement already matches the document.";
    case PlaceVoxelStampPreparationStatus::InvalidInput:
        return "Stamp placement requires an active Stamp and document sub-model.";
    case PlaceVoxelStampPreparationStatus::InvalidPreview:
        return "Stamp preview does not exactly match the active Stamp.";
    case PlaceVoxelStampPreparationStatus::InvalidPreviewPosition:
        return "Stamp preview contains a position outside the active document sub-model.";
    case PlaceVoxelStampPreparationStatus::PaletteMappingFailed:
        return "Stamp palette mapping could not be prepared.";
    case PlaceVoxelStampPreparationStatus::AllocationFailure:
        return "Stamp placement operation allocation failed.";
    }

    return "Unknown stamp placement preparation status.";
}

/// A prepared existing VoxelEditOperation; this component never commits or
/// mutates. The caller passes Operation unchanged to VoxelEditHistory::Execute.
struct PlaceVoxelStampPreparation final
{
    PlaceVoxelStampPreparationStatus Status = PlaceVoxelStampPreparationStatus::InvalidInput;
    PaletteMappingStatus PaletteStatus = PaletteMappingStatus::Success;
    VoxelEditOperation Operation{};

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == PlaceVoxelStampPreparationStatus::Ready;
    }

    [[nodiscard]] bool IsNoChange() const noexcept
    {
        return Status == PlaceVoxelStampPreparationStatus::NoChange;
    }
};

/// Thin transaction adapter. All positions, palette indices, overlaps,
/// bounds, diagnostics and before/after voxel states have already been decided
/// by StampPlacementPlanner. This function never reads a document or replans.
[[nodiscard]] PlaceVoxelStampPreparation PreparePlaceVoxelStampOperation(
    const StampPlacementPlan& plan) noexcept;

/// Validates that the immutable plan still belongs to the active document,
/// generation and revision, prepares its stored before/after data, then
/// submits exactly one atomic history operation. Undo and Redo consume only
/// that stored operation and never invoke the planner again.
[[nodiscard]] VoxelEditHistoryResult ExecutePlaceVoxelStampOperation(
    const StampPlacementPlan& plan,
    VoxelEditSession& session,
    VoxelEditHistory& history);

} // namespace VoxelForge::Editor::Stamps
