#include "VoxelPencilPreview.h"

#include <utility>

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
    return Position.has_value() &&
        (Status == VoxelPlacementPreviewStatus::Valid ||
         Status == VoxelPlacementPreviewStatus::OutOfBounds ||
         Status == VoxelPlacementPreviewStatus::Occupied);
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
    const bool pencilActive,
    const std::optional<Asset::Voxel::VoxelPosition> workplaneTarget,
    const SmartBrushState state) noexcept
{
    if (!pencilActive || document == nullptr)
    {
        return {};
    }
    const auto dimensions = document->GetDimensions(subModelIndex);
    if (!dimensions) return {};

    Asset::Voxel::VoxelPosition adjacent{};
    Asset::Voxel::VoxelPosition placementNormal{0, 1, 0};
    if (workplaneTarget)
    {
        adjacent = *workplaneTarget;
    }
    else
    {
        if (!hit || hit->Face == VoxelHitFace::None ||
            hit->SubModelIndex != subModelIndex)
            return {};
        const Asset::Voxel::VoxelPosition expectedAdjacent =
            CalculateAdjacent(*hit);
        if (hit->AdjacentPosition != expectedAdjacent) return {};
        adjacent = hit->AdjacentPosition;
        placementNormal = VoxelHitFaceIntegerNormal(hit->Face);
    }
    SmartBrushResult brush = SmartBrushEngine::Resolve({
        *dimensions,
        state,
        {adjacent, placementNormal},
        [document, subModelIndex](const Asset::Voxel::VoxelPosition position)
        {
            return document->HasVoxel(position, subModelIndex);
        }});
    if (brush.Code == SmartBrushResultCode::OutOfBounds)
        return {VoxelPlacementPreviewStatus::OutOfBounds, adjacent,
            VoxelPreviewTool::Pencil, std::move(brush.Positions), {}, {},
            brush.Statistics, std::move(brush.RenderPlan)};
    if (brush.Code != SmartBrushResultCode::Valid) return {};
    return {
        brush.HasAddablePositions()
            ? VoxelPlacementPreviewStatus::Valid
            : VoxelPlacementPreviewStatus::Occupied,
        adjacent,
        VoxelPreviewTool::Pencil,
        std::move(brush.Positions),
        std::move(brush.AddablePositions),
        std::move(brush.ExistingPositions),
        brush.Statistics,
        std::move(brush.RenderPlan)};
}

VoxelPlacementPreview EvaluateVoxelPencilPreview(
    const Asset::Voxel::VoxelDocument* document,
    const std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    const bool pencilActive,
    const std::optional<Asset::Voxel::VoxelPosition> workplaneTarget,
    const VoxelBrushShape brush,
    const int brushSize) noexcept
{
    SmartBrushState state;
    state.Shape = brush == VoxelBrushShape::Sphere
        ? SmartBrushShape::Sphere : SmartBrushShape::Cube;
    state.Size = brushSize;
    return EvaluateVoxelPencilPreview(
        document, subModelIndex, hit, pencilActive, workplaneTarget, state);
}

VoxelPlacementPreview EvaluateVoxelEraserPreview(
    const Asset::Voxel::VoxelDocument* document,
    const std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    const bool eraserActive,
    const bool blocked) noexcept
{
    if (!eraserActive) return {};
    if (blocked)
        return {
            VoxelPlacementPreviewStatus::Blocked,
            std::nullopt,
            VoxelPreviewTool::Eraser};
    if (document == nullptr || !hit || hit->Face == VoxelHitFace::None ||
        hit->SubModelIndex != subModelIndex)
    {
        return {};
    }
    if (hit->DocumentRevision != document->GetRevision())
    {
        return {
            VoxelPlacementPreviewStatus::TargetMissing,
            std::nullopt,
            VoxelPreviewTool::Eraser};
    }
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
    {
        return {
            VoxelPlacementPreviewStatus::TargetMissing,
            position,
            VoxelPreviewTool::Eraser};
    }
    return {
        VoxelPlacementPreviewStatus::Valid,
        position,
        VoxelPreviewTool::Eraser};
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
