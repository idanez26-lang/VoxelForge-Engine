#include "SmartTools/SmartToolLineConstraintResolver.h"

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

[[nodiscard]] std::int64_t AbsoluteDelta(const std::int32_t from,
    const std::int32_t to) noexcept
{
    const std::int64_t delta = static_cast<std::int64_t>(to) - from;
    return delta < 0 ? -delta : delta;
}
}

void SmartToolLineConstraintResolver::Begin(const Position pointA) noexcept
{
    pointA_ = pointA;
    lockedAxis_.reset();
}

void SmartToolLineConstraintResolver::Reset() noexcept
{
    pointA_.reset();
    lockedAxis_.reset();
}

bool SmartToolLineConstraintResolver::IsActive() const noexcept
{
    return pointA_.has_value();
}

std::optional<SmartToolLineAxis>
SmartToolLineConstraintResolver::LockedAxis() const noexcept
{
    return lockedAxis_;
}

SmartToolLineConstraintResult SmartToolLineConstraintResolver::Resolve(
    const Position freeEndpoint, const bool shiftHeld) noexcept
{
    if (!pointA_ || !shiftHeld)
    {
        // Shift is a temporary modifier: releasing it restores the raw B and
        // deliberately unlocks the previous axis for a later press.
        lockedAxis_.reset();
        return {freeEndpoint, std::nullopt};
    }

    if (freeEndpoint == *pointA_)
    {
        // MouseDown commonly begins with B == A. Do not manufacture an X
        // lock from the all-zero tie, otherwise the first real Y/Z movement
        // would be held by hysteresis on the wrong axis. A lock chosen by a
        // prior non-zero movement is retained when returning to A.
        return {freeEndpoint, lockedAxis_};
    }

    const SmartToolLineAxis candidate = DominantAxis(*pointA_, freeEndpoint);
    if (!lockedAxis_)
        lockedAxis_ = candidate;
    else if (candidate != *lockedAxis_)
    {
        const std::int64_t candidateMagnitude = AxisMagnitude(candidate,
            *pointA_, freeEndpoint);
        const std::int64_t lockedMagnitude = AxisMagnitude(*lockedAxis_,
            *pointA_, freeEndpoint);
        if (candidateMagnitude > lockedMagnitude + AxisSwitchHysteresisVoxels)
            lockedAxis_ = candidate;
    }

    return {ConstrainToAxis(*pointA_, freeEndpoint, *lockedAxis_), lockedAxis_};
}

SmartToolLineAxis SmartToolLineConstraintResolver::DominantAxis(
    const Position pointA, const Position pointB) noexcept
{
    const std::int64_t dx = AbsoluteDelta(pointA.X, pointB.X);
    const std::int64_t dy = AbsoluteDelta(pointA.Y, pointB.Y);
    const std::int64_t dz = AbsoluteDelta(pointA.Z, pointB.Z);
    // The explicit X -> Y -> Z priority makes all equalities deterministic.
    if (dx >= dy && dx >= dz) return SmartToolLineAxis::X;
    if (dy >= dz) return SmartToolLineAxis::Y;
    return SmartToolLineAxis::Z;
}

std::int64_t SmartToolLineConstraintResolver::AxisMagnitude(
    const SmartToolLineAxis axis, const Position pointA,
    const Position pointB) noexcept
{
    switch (axis)
    {
    case SmartToolLineAxis::X: return AbsoluteDelta(pointA.X, pointB.X);
    case SmartToolLineAxis::Y: return AbsoluteDelta(pointA.Y, pointB.Y);
    case SmartToolLineAxis::Z: return AbsoluteDelta(pointA.Z, pointB.Z);
    }
    return 0;
}

Position SmartToolLineConstraintResolver::ConstrainToAxis(const Position pointA,
    const Position pointB, const SmartToolLineAxis axis) noexcept
{
    Position constrained = pointA;
    switch (axis)
    {
    case SmartToolLineAxis::X: constrained.X = pointB.X; break;
    case SmartToolLineAxis::Y: constrained.Y = pointB.Y; break;
    case SmartToolLineAxis::Z: constrained.Z = pointB.Z; break;
    }
    return constrained;
}
} // namespace VoxelForge::Editor
