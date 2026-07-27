#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{
// World-grid axes only. The resolver deliberately has no camera, input,
// renderer, document, planner, or line-rasterization dependency.
enum class SmartToolLineAxis : std::uint8_t
{
    X,
    Y,
    Z
};

[[nodiscard]] constexpr const char* SmartToolLineAxisLabel(
    const SmartToolLineAxis axis) noexcept
{
    switch (axis)
    {
    case SmartToolLineAxis::X: return "X";
    case SmartToolLineAxis::Y: return "Y";
    case SmartToolLineAxis::Z: return "Z";
    }
    return "";
}

struct SmartToolLineConstraintResult final
{
    Asset::Voxel::VoxelPosition Endpoint{};
    std::optional<SmartToolLineAxis> Axis;
};

// Resolves only the live B endpoint of a Line drag. It never rasterizes a
// line: SmartToolPlanner remains the one authority for planned voxels.
class SmartToolLineConstraintResolver final
{
public:
    // A rival axis must exceed the locked axis by at least two voxel units to
    // take over. This absorbs small pointer/picking fluctuations near ties.
    static constexpr std::int64_t AxisSwitchHysteresisVoxels = 1;

    void Begin(Asset::Voxel::VoxelPosition pointA) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::optional<SmartToolLineAxis> LockedAxis() const noexcept;
    [[nodiscard]] SmartToolLineConstraintResult Resolve(
        Asset::Voxel::VoxelPosition freeEndpoint, bool shiftHeld) noexcept;

private:
    [[nodiscard]] static SmartToolLineAxis DominantAxis(
        Asset::Voxel::VoxelPosition pointA,
        Asset::Voxel::VoxelPosition pointB) noexcept;
    [[nodiscard]] static std::int64_t AxisMagnitude(
        SmartToolLineAxis axis, Asset::Voxel::VoxelPosition pointA,
        Asset::Voxel::VoxelPosition pointB) noexcept;
    [[nodiscard]] static Asset::Voxel::VoxelPosition ConstrainToAxis(
        Asset::Voxel::VoxelPosition pointA,
        Asset::Voxel::VoxelPosition pointB, SmartToolLineAxis axis) noexcept;

    std::optional<Asset::Voxel::VoxelPosition> pointA_;
    std::optional<SmartToolLineAxis> lockedAxis_;
};
} // namespace VoxelForge::Editor
