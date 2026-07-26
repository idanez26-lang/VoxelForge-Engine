#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace VoxelForge::Editor::Stamps
{
namespace
{
[[nodiscard]] bool IsValidPreview(
    const VoxelStamp& stamp,
    const VoxelPreviewData& preview) noexcept
{
    return preview.IsActive() && preview.State != VoxelPreviewState::Invalid &&
           preview.SourceId == stamp.Identity().Id &&
           preview.SourceRevision == stamp.Identity().ContentHash &&
           preview.Voxels.size() == stamp.Voxels().size();
}

[[nodiscard]] bool IsWithinDimensions(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
           static_cast<std::uint64_t>(position.X) < dimensions.X &&
           static_cast<std::uint64_t>(position.Y) < dimensions.Y &&
           static_cast<std::uint64_t>(position.Z) < dimensions.Z;
}

[[nodiscard]] bool IsPositionLess(
    const Asset::Voxel::VoxelPosition& left,
    const Asset::Voxel::VoxelPosition& right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}
} // namespace

PlaceVoxelStampPreparation PreparePlaceVoxelStampOperation(
    const PlaceVoxelStampRequest& request) noexcept
{
    if (request.Stamp == nullptr || request.Preview == nullptr || request.Document == nullptr ||
        request.Document->GetModel(request.SubModelIndex) == nullptr)
    {
        return {};
    }

    const VoxelStamp& stamp = *request.Stamp;
    const VoxelPreviewData& preview = *request.Preview;
    if (!IsValidPreview(stamp, preview))
    {
        return {.Status = PlaceVoxelStampPreparationStatus::InvalidPreview};
    }

    try
    {
        PaletteMappingRequest paletteRequest{
            .StampPalette = stamp.Palette(),
            .StampVoxels = stamp.Voxels(),
            .DocumentPalette = request.Document->GetPaletteSnapshot(),
            .PaletteCapacity = request.PaletteCapacity,
            .ReservedDocumentPaletteIndex = request.ReservedDocumentPaletteIndex};
        for (std::size_t modelIndex = 0U; modelIndex < request.Document->GetModelCount(); ++modelIndex)
        {
            request.Document->GetModel(modelIndex)->ForEachVoxel(
                [&paletteRequest](
                    const Asset::Voxel::VoxelPosition,
                    const Asset::Voxel::Voxel voxel)
                {
                    paletteRequest.OccupiedDocumentPaletteIndices[voxel.PaletteIndex] = true;
                });
        }
        const PaletteMappingResult paletteMapping = PaletteMappingEngine::Plan(paletteRequest);
        if (!paletteMapping.IsSuccess())
        {
            return {.Status = PlaceVoxelStampPreparationStatus::PaletteMappingFailed,
                    .PaletteStatus = paletteMapping.Status};
        }

        std::array<std::uint8_t, 256U> localToDocument{};
        for (const PaletteMappingEntry& entry : paletteMapping.Plan.LocalToDocument)
        {
            localToDocument[entry.LocalColorId] = entry.DocumentPaletteIndex;
        }

        const auto dimensions = request.Document->GetDimensions(request.SubModelIndex);
        if (!dimensions)
        {
            return {};
        }
        std::vector<Asset::Voxel::VoxelPosition> positions;
        positions.reserve(preview.Voxels.size());
        for (const VoxelPreviewVoxel& voxel : preview.Voxels)
        {
            if (!IsWithinDimensions(voxel.Position, *dimensions))
            {
                return {.Status = PlaceVoxelStampPreparationStatus::InvalidPreviewPosition};
            }
            positions.push_back(voxel.Position);
        }
        std::sort(positions.begin(), positions.end(), IsPositionLess);
        if (std::adjacent_find(positions.begin(), positions.end()) != positions.end())
        {
            return {.Status = PlaceVoxelStampPreparationStatus::InvalidPreview};
        }

        VoxelEditOperation operation;
        operation.Label = "Place Voxel Stamp";
        operation.Changes.reserve(preview.Voxels.size());
        for (std::size_t index = 0U; index < preview.Voxels.size(); ++index)
        {
            const StampVoxel& stampVoxel = stamp.Voxels()[index];
            const VoxelPreviewVoxel& previewVoxel = preview.Voxels[index];
            const StampColor expectedColor = stamp.Palette()[stampVoxel.LocalColorId].Color;
            if (previewVoxel.Color != expectedColor)
            {
                return {.Status = PlaceVoxelStampPreparationStatus::InvalidPreview};
            }

            const std::optional<Asset::Voxel::Voxel> existing = request.Document->GetVoxel(
                previewVoxel.Position, request.SubModelIndex);
            const std::uint8_t paletteIndexAfter = localToDocument[stampVoxel.LocalColorId];
            if (!existing || existing->PaletteIndex != paletteIndexAfter)
            {
                operation.Changes.push_back({
                    .SubModelIndex = request.SubModelIndex,
                    .Position = previewVoxel.Position,
                    .ExistedBefore = existing.has_value(),
                    .PaletteIndexBefore = existing ? existing->PaletteIndex : 0U,
                    .ExistsAfter = true,
                    .PaletteIndexAfter = paletteIndexAfter});
            }
        }
        if (paletteMapping.Plan.HasPaletteChanges())
        {
            operation.PaletteChange = std::make_shared<Asset::Voxel::VoxelDocumentPaletteChange>(
                Asset::Voxel::VoxelDocumentPaletteChange{
                    .Before = paletteRequest.DocumentPalette,
                    .After = paletteMapping.Plan.FinalDocumentPalette});
        }
        if (operation.Changes.empty() && !operation.PaletteChange)
        {
            return {.Status = PlaceVoxelStampPreparationStatus::NoChange,
                    .PaletteStatus = paletteMapping.Status};
        }

        return {.Status = PlaceVoxelStampPreparationStatus::Ready,
                .PaletteStatus = paletteMapping.Status,
                .Operation = std::move(operation)};
    }
    catch (const std::bad_alloc&)
    {
        return {.Status = PlaceVoxelStampPreparationStatus::AllocationFailure};
    }
}

} // namespace VoxelForge::Editor::Stamps
