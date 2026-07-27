#include "SmartTools/SmartBrushPreviewResolver.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
float ResolveAlpha(const float alpha) noexcept
{
    return std::clamp(alpha, 0.0F, 1.0F);
}

void AppendGhost(SmartBrushPreviewResult& result,
    const Asset::Voxel::VoxelPosition position, const GhostVoxelState state,
    const std::array<float, 4>& color, const float alpha)
{
    result.GhostVoxels.push_back({position, state, color, alpha});
}
}

SmartBrushPreviewResult SmartBrushPreviewResolver::Resolve(
    const SmartToolPlan& plan,
    const std::array<float, 4>& activePaletteColor, const float requestedAlpha)
{
    SmartBrushPreviewResult result;
    const float alpha = ResolveAlpha(requestedAlpha);
    const SmartBrushResult& brush = plan.BrushResult();
    result.Code = brush.Code;
    result.Error = brush.Error;
    result.RenderPlan = brush.RenderPlan;
    result.Statistics = {
        plan.Statistics().Total, 0U, 0U, plan.Statistics().Clipped};

    if (brush.Code != SmartBrushResultCode::Valid &&
        brush.Code != SmartBrushResultCode::OutOfBounds)
    {
        AppendGhost(result, plan.Placement().Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }

    result.GhostVoxels.reserve(plan.Cells().size());
    result.AffectedPositions.reserve(plan.Cells().size());
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        bool affected = false;
        GhostVoxelState state = GhostVoxelState::Invalid;
        std::array<float, 4> color = GhostPreviewStyle::Invalid;
        switch (cell.PreviewState)
        {
        case SmartToolPlanPreviewState::Added:
            affected = true;
            state = GhostVoxelState::Added;
            color = GhostPreviewStyle::Added;
            break;
        case SmartToolPlanPreviewState::Erased:
            affected = true;
            state = GhostVoxelState::Erased;
            color = GhostPreviewStyle::Erased;
            break;
        case SmartToolPlanPreviewState::Painted:
        case SmartToolPlanPreviewState::Replaced:
            affected = true;
            state = GhostVoxelState::Painted;
            color = activePaletteColor;
            break;
        case SmartToolPlanPreviewState::Ignored:
            state = GhostVoxelState::Ignored;
            color = GhostPreviewStyle::Ignored;
            break;
        case SmartToolPlanPreviewState::Clipped:
            state = GhostVoxelState::Clipped;
            color = GhostPreviewStyle::Clipped;
            break;
        case SmartToolPlanPreviewState::Invalid:
        default: break;
        }
        if (affected)
        {
            ++result.Statistics.Affected;
            result.AffectedPositions.push_back(cell.WorldPosition);
        }
        else if (state != GhostVoxelState::Clipped) ++result.Statistics.Ignored;
        AppendGhost(result, cell.WorldPosition, state, color, alpha);
    }
    return result;
}
} // namespace VoxelForge::Editor
