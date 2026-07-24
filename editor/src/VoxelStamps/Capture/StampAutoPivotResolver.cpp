#include "VoxelStamps/Capture/StampAutoPivotResolver.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace VoxelForge::Editor::Stamps
{
namespace
{

constexpr std::uint32_t AutoPivotPolicyVersion = 1U;

[[nodiscard]] std::int32_t FixedExtent(const std::uint32_t dimension) noexcept
{
    const std::int64_t extent =
        static_cast<std::int64_t>(dimension) * StampFixedPoint::UnitsPerVoxel;
    return extent > std::numeric_limits<std::int32_t>::max()
        ? std::numeric_limits<std::int32_t>::max()
        : static_cast<std::int32_t>(extent);
}

[[nodiscard]] StampFixedPoint CenterOf(const StampBounds& bounds) noexcept
{
    return {
        .X = FixedExtent(bounds.Dimensions.X) / 2,
        .Y = FixedExtent(bounds.Dimensions.Y) / 2,
        .Z = FixedExtent(bounds.Dimensions.Z) / 2};
}

[[nodiscard]] StampNormal PrincipalAxis(const StampNormal normal) noexcept
{
    const int nonZeroCount = (normal.X != 0 ? 1 : 0) +
        (normal.Y != 0 ? 1 : 0) + (normal.Z != 0 ? 1 : 0);
    if (nonZeroCount != 1) return {};
    if (normal.X != 0) return {.X = static_cast<std::int8_t>(normal.X > 0 ? 1 : -1)};
    if (normal.Y != 0) return {.Y = static_cast<std::int8_t>(normal.Y > 0 ? 1 : -1)};
    return {.Z = static_cast<std::int8_t>(normal.Z > 0 ? 1 : -1)};
}

[[nodiscard]] StampFixedPoint CornerOf(
    const StampBounds& bounds,
    const StampNormal direction) noexcept
{
    // Zero components deliberately resolve to the minimum coordinate. This is
    // the fixed X/Y/Z tie-break used by V1 for an axis not named by the grid
    // direction.
    return {
        .X = direction.X > 0 ? FixedExtent(bounds.Dimensions.X) : 0,
        .Y = direction.Y > 0 ? FixedExtent(bounds.Dimensions.Y) : 0,
        .Z = direction.Z > 0 ? FixedExtent(bounds.Dimensions.Z) : 0};
}

[[nodiscard]] bool IsTopHorizontal(const StampNormal normal) noexcept
{
    return normal == StampNormal{.X = 0, .Y = 1, .Z = 0};
}

[[nodiscard]] bool IsVertical(const StampNormal normal) noexcept
{
    return (normal.X != 0 && normal.Y == 0 && normal.Z == 0) ||
           (normal.X == 0 && normal.Y == 0 && normal.Z != 0);
}

[[nodiscard]] StampNormal ContextNormal(const StampPivotContext& context) noexcept
{
    const StampNormal source = context.PlacementKind == StampPivotPlacementKind::Workplane
        ? context.WorkplaneNormal
        : context.SurfaceNormal;
    return PrincipalAxis(source);
}

[[nodiscard]] bool IsInside(
    const StampFixedPoint point,
    const StampBounds& bounds) noexcept
{
    return point.X >= 0 && point.Y >= 0 && point.Z >= 0 &&
        point.X <= FixedExtent(bounds.Dimensions.X) &&
        point.Y <= FixedExtent(bounds.Dimensions.Y) &&
        point.Z <= FixedExtent(bounds.Dimensions.Z);
}

[[nodiscard]] StampPivot MakeCenter(const StampPivotContext& context) noexcept
{
    return {
        .RequestedMode = context.RequestedMode,
        .ResolvedMode = StampPivotMode::Center,
        .LocalPosition = CenterOf(context.Bounds),
        .AutoPolicyVersion = AutoPivotPolicyVersion};
}

[[nodiscard]] StampPivot MakeBottomCenter(const StampPivotContext& context) noexcept
{
    StampFixedPoint position = CenterOf(context.Bounds);
    position.Y = 0;
    return {
        .RequestedMode = context.RequestedMode,
        .ResolvedMode = StampPivotMode::BottomCenter,
        .LocalPosition = position,
        .LocalNormal = {.X = 0, .Y = 1, .Z = 0},
        .AutoPolicyVersion = AutoPivotPolicyVersion};
}

[[nodiscard]] StampPivot MakeSurface(const StampPivotContext& context) noexcept
{
    const StampNormal normal = ContextNormal(context);
    StampFixedPoint position = CenterOf(context.Bounds);
    if (normal.X > 0) position.X = FixedExtent(context.Bounds.Dimensions.X);
    if (normal.X < 0) position.X = 0;
    if (normal.Y > 0) position.Y = FixedExtent(context.Bounds.Dimensions.Y);
    if (normal.Y < 0) position.Y = 0;
    if (normal.Z > 0) position.Z = FixedExtent(context.Bounds.Dimensions.Z);
    if (normal.Z < 0) position.Z = 0;
    if (context.HasSurfaceAttachment && IsInside(context.SurfaceAttachment, context.Bounds))
    {
        position = context.SurfaceAttachment;
    }
    return {
        .RequestedMode = context.RequestedMode,
        .ResolvedMode = StampPivotMode::Surface,
        .LocalPosition = position,
        .LocalNormal = normal,
        .AutoPolicyVersion = AutoPivotPolicyVersion};
}

[[nodiscard]] StampPivot MakeCorner(const StampPivotContext& context) noexcept
{
    return {
        .RequestedMode = context.RequestedMode,
        .ResolvedMode = StampPivotMode::Corner,
        .LocalPosition = CornerOf(context.Bounds, context.GridDirection),
        .AutoPolicyVersion = AutoPivotPolicyVersion};
}

} // namespace

StampPivot ResolveAutoPivot(const StampPivotContext& context) noexcept
{
    switch (context.RequestedMode)
    {
    case StampPivotMode::Center: return MakeCenter(context);
    case StampPivotMode::BottomCenter: return MakeBottomCenter(context);
    case StampPivotMode::Surface:
        // A Surface pivot without one unambiguous, principal attachment normal
        // would encode an incoherent support relationship. V1's documented
        // deterministic fallback is Center rather than a zero-normal Surface.
        return ContextNormal(context) == StampNormal{} ? MakeCenter(context) : MakeSurface(context);
    case StampPivotMode::Corner: return MakeCorner(context);
    case StampPivotMode::Auto: break;
    }

    if (context.PlacementKind == StampPivotPlacementKind::PreciseGrid)
    {
        return MakeCorner(context);
    }

    const StampNormal normal = ContextNormal(context);
    if ((context.PlacementKind == StampPivotPlacementKind::Surface ||
         context.PlacementKind == StampPivotPlacementKind::Workplane) &&
        IsTopHorizontal(normal))
    {
        return MakeBottomCenter(context);
    }
    if (context.PlacementKind == StampPivotPlacementKind::Surface && IsVertical(normal))
    {
        return MakeSurface(context);
    }

    return MakeCenter(context);
}

} // namespace VoxelForge::Editor::Stamps
