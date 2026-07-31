#pragma once

#include "SmartTools/SmartToolResult.h"
#include "SmartTools/SmartToolRequest.h"
#include "SmartTools/PencilCompactPlan.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace VoxelForge::Editor
{
// The sole SMART-01 geometry adapter. Future geometries are deliberately
// rejected instead of being silently approximated by Pencil.
class SmartToolPlanner final
{
public:
    explicit SmartToolPlanner(
        std::size_t fillCellLimit = MaximumSmartFillCells) noexcept
        : fillCellLimit_(fillCellLimit == 0U ? 1U : fillCellLimit)
    {
    }
    [[nodiscard]] SmartToolResult Plan(const SmartToolRequest& request);

    // Phase-B compact Pencil planning. This is deliberately a separate public
    // boundary from Plan(): it resolves an exact procedural footprint without
    // reading a document or materializing a cell list. Commit integration is
    // introduced later and must consume this immutable plan through Iterate().
    [[nodiscard]] PencilCompactPlanResult PlanPencilCompact(
        const PencilCompactRequest& request);
    // Resolves one pointer-update batch.  The caller may interpolate several
    // voxel centres, but crossing this boundary remains one planner request
    // for the whole input update.  Each returned plan stays immutable and
    // procedural; no hover cell list is materialized here.
    [[nodiscard]] std::vector<PencilCompactPlanResult> PlanPencilCompactBatch(
        const PencilCompactRequest& request,
        std::span<const Asset::Voxel::VoxelPosition> centres);
    [[nodiscard]] std::size_t PencilCompactFootprintCacheSize() const noexcept;

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
    std::size_t fillCellLimit_ = MaximumSmartFillCells;
    std::uint64_t nextPlanId_ = 1U;
    std::uint64_t nextPencilCompactPlanId_ = 1U;
    std::unordered_map<SmartBrushCompactCacheKey,
        std::shared_ptr<const SmartBrushCompactFootprint>,
        SmartBrushCompactCacheKeyHash> pencilCompactFootprints_;
};
} // namespace VoxelForge::Editor
