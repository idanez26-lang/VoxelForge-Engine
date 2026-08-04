#include "VoxelPencilPreview.h"

namespace VoxelForge::Editor
{
bool VoxelToolPlacementPreview::IsVisible() const noexcept
{
    return Position.has_value() &&
        (Status == VoxelPlacementPreviewStatus::Valid ||
         Status == VoxelPlacementPreviewStatus::OutOfBounds ||
         Status == VoxelPlacementPreviewStatus::Occupied);
}

bool VoxelToolPlacementPreview::IsValid() const noexcept
{
    return Status == VoxelPlacementPreviewStatus::Valid && Position.has_value();
}

VoxelToolPlacementPreview EvaluateVoxelEraserPreview(
    const Asset::Voxel::VoxelDocument* document, const std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit, const bool eraserActive,
    const bool blocked) noexcept
{
    if (!eraserActive) return {};
    if (blocked)
        return {VoxelPlacementPreviewStatus::Blocked, std::nullopt,
            VoxelPreviewTool::Eraser};
    if (document == nullptr || !hit || hit->Face == VoxelHitFace::None ||
        hit->SubModelIndex != subModelIndex)
        return {};
    if (hit->DocumentRevision != document->GetRevision())
        return {VoxelPlacementPreviewStatus::TargetMissing, std::nullopt,
            VoxelPreviewTool::Eraser};
    const auto dimensions = document->GetDimensions(subModelIndex);
    if (!dimensions) return {};
    const Asset::Voxel::VoxelPosition position{
        static_cast<std::int32_t>(hit->Coordinates.X),
        static_cast<std::int32_t>(hit->Coordinates.Y),
        static_cast<std::int32_t>(hit->Coordinates.Z)};
    const bool inside = position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions->X &&
        static_cast<std::uint32_t>(position.Y) < dimensions->Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions->Z;
    if (!inside || !document->HasVoxel(position, subModelIndex))
        return {VoxelPlacementPreviewStatus::TargetMissing, position,
            VoxelPreviewTool::Eraser};
    return {VoxelPlacementPreviewStatus::Valid, position, VoxelPreviewTool::Eraser};
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
    case VoxelPlacementPreviewStatus::TargetMissing: return "Target missing";
    case VoxelPlacementPreviewStatus::Blocked: return "Blocked";
    }
    return "Unavailable";
}
} // namespace VoxelForge::Editor
