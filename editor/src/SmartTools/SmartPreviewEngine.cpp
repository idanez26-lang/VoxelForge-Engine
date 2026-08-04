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

[[nodiscard]] VoxelPreviewSemantic SemanticFor(
    const GhostVoxelState state) noexcept
{
    switch (state)
    {
    case GhostVoxelState::Added: return VoxelPreviewSemantic::Added;
    case GhostVoxelState::Erased: return VoxelPreviewSemantic::Erased;
    case GhostVoxelState::Painted: return VoxelPreviewSemantic::Painted;
    case GhostVoxelState::Ignored: return VoxelPreviewSemantic::Ignored;
    case GhostVoxelState::Clipped: return VoxelPreviewSemantic::Clipped;
    case GhostVoxelState::Invalid:
    default: return VoxelPreviewSemantic::Invalid;
    }
}

[[nodiscard]] VoxelPreviewStats AggregateStatistics(
    const SmartToolPlan& plan) noexcept
{
    const SmartToolPlanStatistics& source = plan.Statistics();
    VoxelPreviewStats result;
    result.Total = source.Total;
    result.Invalid = source.Invalid;
    result.Clipped = source.Clipped;
    result.Ignored = source.Unchanged;
    switch (plan.Action())
    {
    case SmartAction::Add: result.Added = source.Changed; break;
    case SmartAction::Erase: result.Erased = source.Changed; break;
    case SmartAction::Paint: result.Painted = source.Changed; break;
    default: result.Painted = source.Changed; break;
    }
    return result;
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
        std::vector<VoxelPreviewInstance> instances;
        instances.reserve(plan.Cells().size());
        data.GhostVoxels.reserve(plan.Cells().size());
        data.AffectedPositions = plan.AffectedPositions();
        for (const SmartToolPlanCell& cell : plan.Cells())
        {
            const GhostVoxelState state = StateFor(cell.PreviewState);
            const std::array<float, 4U> color = BaseColor(cell);
            data.GhostVoxels.push_back(
                {cell.WorldPosition, state, color, 1.0F});
            instances.push_back({
                cell.WorldPosition, SemanticFor(state), color, 1.0F});
        }
        data.Placement = VoxelPlacementPreview::FromInstances(
            plan.PlanId(), std::move(instances));
    }
    else
        data.Placement = VoxelPlacementPreview::Aggregate(
            plan.PlanId(), AggregateStatistics(plan));
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
