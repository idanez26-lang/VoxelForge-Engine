#pragma once

#include "Preview/VoxelPlacementPreview.h"
#include "SmartTools/SmartPreviewTypes.h"
#include "SmartTools/SmartToolPlan.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{
// Renderer-neutral immutable presentation data.  It is built exclusively from
// an immutable SmartToolPlan and deliberately has no document or palette API.
struct SmartPreviewData final
{
    SmartBrushResultCode Code = SmartBrushResultCode::InvalidRequest;
    std::uint64_t PlanId = 0U;
    std::uint64_t Revision = 0U;
    SmartGeometry Geometry = SmartGeometry::Pencil;
    SmartAction Action = SmartAction::Add;
    SmartBrushState Brush{};
    SmartToolPlanStatistics Statistics{};
    SmartToolPlanBounds Bounds{};
    SmartToolPlanPreviewDiagnostics Diagnostics{};
    SmartBrushRenderPlan RenderPlan{};
    std::vector<GhostVoxel> GhostVoxels;
    std::vector<Asset::Voxel::VoxelPosition> AffectedPositions;
    VoxelPlacementPreview Placement;
    std::string Error;

    [[nodiscard]] bool CanCommit() const noexcept
    {
        return Diagnostics.CanCommit;
    }
};

class SmartPreviewEngine final
{
public:
    // Builds exact ghost voxels from the plan's materialized positions,
    // colours, flags and diagnostics.  It does not call a planner, document,
    // palette service or renderer.
    [[nodiscard]] static SmartPreviewData Build(const SmartToolPlan& plan);
};

// One-entry identity cache is sufficient for live hover preview and keeps
// rebuilding bounded to actual immutable plan changes.
class SmartPreviewCache final
{
public:
    [[nodiscard]] const SmartPreviewData& Resolve(SmartToolPlanPtr plan);
    void Clear() noexcept;
    [[nodiscard]] std::size_t BuildCount() const noexcept;

private:
    SmartToolPlanPtr plan_;
    SmartPreviewData data_{};
    std::size_t buildCount_ = 0U;
};
} // namespace VoxelForge::Editor
