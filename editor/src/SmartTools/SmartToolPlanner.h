#pragma once

#include "SmartTools/SmartToolResult.h"
#include "SmartTools/SmartToolRequest.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{
// The sole SMART-01 geometry adapter. Future geometries are deliberately
// rejected instead of being silently approximated by Pencil.
class SmartToolPlanner final
{
public:
    [[nodiscard]] SmartToolResult Plan(const SmartToolRequest& request);

    // Geometry interaction helpers deliberately live beside the sole
    // geometry authority. They only canonicalize the locked plane and B;
    // sampling remains private to Plan().
    [[nodiscard]] static std::optional<SmartToolGeometryPlane>
        MakeGeometryPlane(Asset::Voxel::VoxelPosition origin,
            Asset::Voxel::VoxelPosition normal,
            float surfaceCoordinate) noexcept;
    [[nodiscard]] static Asset::Voxel::VoxelPosition ProjectGeometryEndpoint(
        const SmartToolGeometryPlane& plane,
        Asset::Voxel::VoxelPosition endpoint) noexcept;

private:
    std::uint64_t nextPlanId_ = 1U;
};
} // namespace VoxelForge::Editor
