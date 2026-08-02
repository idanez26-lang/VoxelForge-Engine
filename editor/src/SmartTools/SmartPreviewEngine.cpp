#include "SmartTools/SmartPreviewEngine.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<float, 4> kEmptyColor{0.54F, 0.58F, 0.64F, 1.0F};

[[nodiscard]] GhostVoxelState StateFor(
    const SmartToolPlanPreviewState state) noexcept
{
    switch (state)
    {
    case SmartToolPlanPreviewState::Added: return GhostVoxelState::Added;
    case SmartToolPlanPreviewState::Erased: return GhostVoxelState::Erased;
    case SmartToolPlanPreviewState::Painted:
    case SmartToolPlanPreviewState::Replaced: return GhostVoxelState::Painted;
    case SmartToolPlanPreviewState::Ignored: return GhostVoxelState::Ignored;
    case SmartToolPlanPreviewState::Clipped: return GhostVoxelState::Clipped;
    case SmartToolPlanPreviewState::Invalid:
    default: return GhostVoxelState::Invalid;
    }
}

[[nodiscard]] std::array<float, 4> BaseColor(
    const SmartToolPlanCell& cell) noexcept
{
    if (cell.After.Exists) return cell.AfterColor;
    if (cell.Before.Exists) return cell.BeforeColor;
    return kEmptyColor;
}

}

SmartPreviewData SmartPreviewEngine::Build(const SmartToolPlan& plan)
{
    SmartPreviewData data;
    data.Code = plan.BrushResult().Code;
    data.PlanId = plan.PlanId();
    data.Revision = plan.Revision();
    data.Geometry = plan.Geometry();
    data.Action = plan.Action();
    data.Brush = plan.BrushState();
    data.Statistics = plan.Statistics();
    data.Bounds = plan.Bounds();
    data.Diagnostics = plan.PreviewDiagnostics();
    data.RenderPlan = plan.BrushResult().RenderPlan;
    data.Error = plan.BrushResult().Error;
    // PERF-02a: aggregate render plans (brushes above
    // MaximumDetailedBrushPreviewVoxelCount) are presented as bounds only.
    // Building one ghost per cell would cost O(volume) per pointer update for
    // data the presentation never reads. Statistics, bounds and diagnostics
    // above stay exact either way.
    if (data.RenderPlan.Mode == SmartBrushRenderMode::DetailedCells)
    {
        data.GhostVoxels.reserve(plan.Cells().size());
        data.AffectedPositions = plan.AffectedPositions();
        for (const SmartToolPlanCell& cell : plan.Cells())
        {
            data.GhostVoxels.push_back({cell.WorldPosition,
                StateFor(cell.PreviewState), BaseColor(cell), 1.0F});
        }
    }
    return data;
}

const SmartPreviewData& SmartPreviewCache::Resolve(SmartToolPlanPtr plan)
{
    if (plan_ != plan)
    {
        plan_ = std::move(plan);
        data_ = plan_ ? SmartPreviewEngine::Build(*plan_) : SmartPreviewData{};
        ++buildCount_;
    }
    return data_;
}

void SmartPreviewCache::Clear() noexcept
{
    plan_.reset();
    data_ = {};
}

std::size_t SmartPreviewCache::BuildCount() const noexcept { return buildCount_; }
} // namespace VoxelForge::Editor
