#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelHistory/VoxelEditOperation.h"
#include "VoxelStamps/Palette/PaletteMappingEngine.h"
#include "VoxelStamps/VoxelStamp.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
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

/// Immutable inputs for the thin STAMP-13 orchestration boundary. Preview is
/// already renderer-ready and is used as the single source of placed positions.
struct PlaceVoxelStampRequest final
{
    const VoxelStamp* Stamp = nullptr;
    const VoxelPreviewData* Preview = nullptr;
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::size_t PaletteCapacity = 256U;
    std::uint8_t ReservedDocumentPaletteIndex = 0U;
};

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

/// Coordinates the existing palette mapper with the existing composite edit
/// operation format. It performs no document, history, preview-session, mesh,
/// renderer, Undo, or Redo mutation.
[[nodiscard]] PlaceVoxelStampPreparation PreparePlaceVoxelStampOperation(
    const PlaceVoxelStampRequest& request) noexcept;

} // namespace VoxelForge::Editor::Stamps
