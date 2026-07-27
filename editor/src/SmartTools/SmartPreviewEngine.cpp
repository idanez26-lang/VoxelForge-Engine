#include "SmartTools/SmartPreviewEngine.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<float, 4> kValidTint{0.30F, 0.92F, 0.48F, 1.0F};
constexpr std::array<float, 4> kOverlapTint{1.0F, 0.56F, 0.12F, 1.0F};
constexpr std::array<float, 4> kErrorTint{0.96F, 0.18F, 0.22F, 1.0F};
constexpr std::array<float, 4> kEmptyColor{0.54F, 0.58F, 0.64F, 1.0F};

[[nodiscard]] float ClampAlpha(const float alpha) noexcept
{
    return std::clamp(alpha, 0.0F, 1.0F);
}

[[nodiscard]] std::array<float, 4> Mix(const std::array<float, 4>& base,
    const std::array<float, 4>& tint, const float weight) noexcept
{
    const float clamped = std::clamp(weight, 0.0F, 1.0F);
    return {base[0] * (1.0F - clamped) + tint[0] * clamped,
        base[1] * (1.0F - clamped) + tint[1] * clamped,
        base[2] * (1.0F - clamped) + tint[2] * clamped,
        base[3]};
}

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

[[nodiscard]] std::array<float, 4> DisplayColor(
    const SmartToolPlanCell& cell) noexcept
{
    const std::array<float, 4> base = BaseColor(cell);
    if (cell.OutOfBounds() || HasSmartToolPlanCellFlag(
            cell.Flags, SmartToolPlanCellFlag::Invalid))
        return Mix(base, kErrorTint, 0.62F);
    if (cell.Overlap()) return Mix(base, kOverlapTint, 0.40F);
    if (HasSmartToolPlanCellFlag(cell.Flags, SmartToolPlanCellFlag::NoChange))
        return Mix(base, kOverlapTint, 0.20F);
    return Mix(base, kValidTint, 0.20F);
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
    data.GhostVoxels.reserve(plan.Cells().size());
    data.AffectedPositions = plan.AffectedPositions();
    const float alpha = ClampAlpha(plan.PreviewAlpha());
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        data.GhostVoxels.push_back({cell.WorldPosition, StateFor(cell.PreviewState),
            DisplayColor(cell), alpha});
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
