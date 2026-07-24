#pragma once

#include "VoxelStamps/StampTypes.h"

#include <cstdint>

namespace VoxelForge::Editor::Stamps
{

enum class StampPivotPlacementKind : std::uint8_t
{
    Free,
    Surface,
    Workplane,
    PreciseGrid
};

/// Canonical, already-local input to the V1 Auto Pivot policy. Coordinates are
/// fixed-point voxel units and normals/directions are discrete integer axes.
/// No pointer, frame, renderer, or document state is part of this contract.
struct StampPivotContext final
{
    StampBounds Bounds{};
    StampPivotMode RequestedMode = StampPivotMode::Auto;
    StampPivotPlacementKind PlacementKind = StampPivotPlacementKind::Free;
    StampNormal SurfaceNormal{};
    StampNormal WorkplaneNormal{};
    StampNormal GridDirection{};
    bool HasSurfaceAttachment = false;
    StampFixedPoint SurfaceAttachment{};

    [[nodiscard]] bool operator==(const StampPivotContext&) const noexcept = default;
};

/// Resolves the approved V1 policy in this order: explicit override, precise
/// grid corner, top horizontal bottom-center, vertical surface, then center.
/// The result is deterministic for identical contexts and policy version.
[[nodiscard]] StampPivot ResolveAutoPivot(
    const StampPivotContext& context) noexcept;

} // namespace VoxelForge::Editor::Stamps
