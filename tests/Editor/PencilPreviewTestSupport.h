#pragma once

// Test-only compatibility assertions for the removed historical Pencil
// preview API. Production Pencil preview is SmartToolPlan -> resolver only.
#include "SmartTools/SmartBrushPreviewResolver.h"
#include "SmartTools/SmartToolController.h"
#include "VoxelTools/VoxelBrush.h"
#include "VoxelTools/VoxelPencilPreview.h"

namespace VoxelForge::Editor
{
inline VoxelToolPlacementPreview EvaluatePencilPlanForTest(
    const Asset::Voxel::VoxelDocument* document, const std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit, const bool pencilActive,
    const std::optional<Asset::Voxel::VoxelPosition> workplaneTarget = std::nullopt,
    SmartBrushState state = {}) noexcept
{
    if (!pencilActive || document == nullptr) return {};
    const auto dimensions = document->GetDimensions(subModelIndex);
    if (!dimensions) return {};
    const bool erasing = state.Mode == SmartBrushMode::Erase;
    const auto target = workplaneTarget ? workplaneTarget
        : hit ? std::optional<Asset::Voxel::VoxelPosition>{erasing
            ? Asset::Voxel::VoxelPosition{static_cast<std::int32_t>(hit->Coordinates.X),
                static_cast<std::int32_t>(hit->Coordinates.Y),
                static_cast<std::int32_t>(hit->Coordinates.Z)}
            : hit->AdjacentPosition} : std::nullopt;
    if (!target || (!workplaneTarget &&
        (!hit || hit->Face == VoxelHitFace::None || hit->SubModelIndex != subModelIndex ||
         (erasing && hit->DocumentRevision != document->GetRevision())))) return {};
    const Asset::Voxel::VoxelPosition normal = workplaneTarget
        ? Asset::Voxel::VoxelPosition{0, 1, 0}
        : VoxelHitFaceIntegerNormal(hit->Face);
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = erasing ? SmartAction::Erase : SmartAction::Add;
    request.BrushRequest = {*dimensions, state, {*target, normal}, {}};
    request.ReadVoxel =
        [document, subModelIndex](const Asset::Voxel::VoxelPosition position)
        {
            const auto voxel = document->GetVoxel(position, subModelIndex);
            return SmartToolVoxelState{
                voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
        };
    request.SourceIdentity = reinterpret_cast<std::uintptr_t>(document);
    request.SourceRevision = document->GetRevision();
    request.SourceSubModelIndex = subModelIndex;
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolPlanPtr plan = controller.ResolvePreview(session, request).Plan;
    if (plan == nullptr) return {};
    const SmartBrushResult& brush = plan->BrushResult();
    if (brush.Code == SmartBrushResultCode::OutOfBounds)
        return {VoxelPlacementPreviewStatus::OutOfBounds, *target,
            VoxelPreviewTool::Pencil, brush.Positions, {}, {}, brush.Statistics,
            brush.RenderPlan};
    if (brush.Code != SmartBrushResultCode::Valid) return {};
    return {erasing ? (!brush.ExistingPositions.empty()
                ? VoxelPlacementPreviewStatus::Valid
                : VoxelPlacementPreviewStatus::Occupied)
            : (brush.HasAddablePositions() ? VoxelPlacementPreviewStatus::Valid
                : VoxelPlacementPreviewStatus::Occupied),
        *target, VoxelPreviewTool::Pencil, brush.Positions, brush.AddablePositions,
        brush.ExistingPositions, brush.Statistics, brush.RenderPlan};
}

inline VoxelToolPlacementPreview EvaluatePencilPlanForTest(
    const Asset::Voxel::VoxelDocument* document, const std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit, const bool pencilActive,
    const std::optional<Asset::Voxel::VoxelPosition> workplaneTarget,
    const VoxelBrushShape brush, const int brushSize) noexcept
{
    SmartBrushState state;
    state.Shape = brush == VoxelBrushShape::Sphere
        ? SmartBrushShape::Sphere : SmartBrushShape::Cube;
    state.Size = brushSize;
    return EvaluatePencilPlanForTest(document, subModelIndex, hit, pencilActive,
        workplaneTarget, state);
}
} // namespace VoxelForge::Editor
