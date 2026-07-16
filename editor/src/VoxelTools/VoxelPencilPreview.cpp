#include "VoxelPencilPreview.h"

namespace VoxelForge::Editor
{
namespace
{
Asset::Voxel::VoxelPosition CalculateAdjacent(
    const VoxelRaycastHit& hit) noexcept
{
    const Asset::Voxel::VoxelPosition normal =
        VoxelHitFaceIntegerNormal(hit.Face);
    return {
        static_cast<std::int32_t>(hit.Coordinates.X) + normal.X,
        static_cast<std::int32_t>(hit.Coordinates.Y) + normal.Y,
        static_cast<std::int32_t>(hit.Coordinates.Z) + normal.Z};
}
}

bool VoxelPlacementPreview::IsVisible() const noexcept
{
    return Status != VoxelPlacementPreviewStatus::Unavailable &&
        Position.has_value();
}

bool VoxelPlacementPreview::IsValid() const noexcept
{
    return Status == VoxelPlacementPreviewStatus::Valid &&
        Position.has_value();
}

VoxelPlacementPreview EvaluateVoxelPencilPreview(
    const Asset::Voxel::VoxelDocument* document,
    const std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    const bool pencilActive) noexcept
{
    if (!pencilActive || document == nullptr || !hit ||
        hit->Face == VoxelHitFace::None ||
        hit->SubModelIndex != subModelIndex)
    {
        return {};
    }
    const auto dimensions = document->GetDimensions(subModelIndex);
    if (!dimensions) return {};

    const Asset::Voxel::VoxelPosition expectedAdjacent =
        CalculateAdjacent(*hit);
    if (hit->AdjacentPosition != expectedAdjacent) return {};
    const Asset::Voxel::VoxelPosition adjacent = hit->AdjacentPosition;
    const bool inside = adjacent.X >= 0 && adjacent.Y >= 0 && adjacent.Z >= 0 &&
        static_cast<std::uint32_t>(adjacent.X) < dimensions->X &&
        static_cast<std::uint32_t>(adjacent.Y) < dimensions->Y &&
        static_cast<std::uint32_t>(adjacent.Z) < dimensions->Z;
    if (!inside)
        return {VoxelPlacementPreviewStatus::OutOfBounds, adjacent};
    if (document->HasVoxel(adjacent, subModelIndex))
        return {VoxelPlacementPreviewStatus::Occupied, adjacent};
    return {VoxelPlacementPreviewStatus::Valid, adjacent};
}

const char* VoxelPlacementPreviewStatusName(
    const VoxelPlacementPreviewStatus status) noexcept
{
    switch (status)
    {
    case VoxelPlacementPreviewStatus::Unavailable: return "Unavailable";
    case VoxelPlacementPreviewStatus::Valid: return "Valid";
    case VoxelPlacementPreviewStatus::OutOfBounds: return "Out of bounds";
    case VoxelPlacementPreviewStatus::Occupied: return "Occupied";
    }
    return "Unavailable";
}

} // namespace VoxelForge::Editor
