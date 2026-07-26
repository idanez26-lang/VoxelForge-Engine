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
    const SmartBrushPreviewRequest& request)
{
    SmartBrushPreviewResult result;
    const float alpha = ResolveAlpha(request.Alpha);
    if (request.Document == nullptr ||
        request.Document->GetModel(request.SubModelIndex) == nullptr)
    {
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "The Smart Brush preview has no editable document.";
        AppendGhost(result, request.Placement.Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }

    const auto dimensions = request.Document->GetDimensions(request.SubModelIndex);
    if (!dimensions)
    {
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "The Smart Brush preview has no valid dimensions.";
        AppendGhost(result, request.Placement.Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }

    const SmartBrushResult brush = SmartBrushEngine::Resolve({
        *dimensions, request.State, request.Placement,
        [document = request.Document, subModelIndex = request.SubModelIndex](
            const Asset::Voxel::VoxelPosition position)
        {
            return document->HasVoxel(position, subModelIndex);
        }});
    result.Code = brush.Code;
    result.Error = brush.Error;
    result.RenderPlan = brush.RenderPlan;
    result.Statistics = {brush.Statistics.Total, 0U, 0U,
        brush.Statistics.Clipped};

    for (const Asset::Voxel::VoxelPosition position : brush.ClippedPositions)
        AppendGhost(result, position, GhostVoxelState::Clipped,
            GhostPreviewStyle::Clipped, alpha);

    if (brush.Code != SmartBrushResultCode::Valid &&
        brush.Code != SmartBrushResultCode::OutOfBounds)
    {
        AppendGhost(result, request.Placement.Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }

    result.GhostVoxels.reserve(result.GhostVoxels.size() + brush.Positions.size());
    result.AffectedPositions.reserve(brush.Positions.size());
    for (const Asset::Voxel::VoxelPosition position : brush.Positions)
    {
        const bool occupied = request.Document->HasVoxel(
            position, request.SubModelIndex);
        const auto voxel = occupied ? request.Document->GetVoxel(
            position, request.SubModelIndex) : std::nullopt;
        bool affected = false;
        GhostVoxelState state = GhostVoxelState::Ignored;
        std::array<float, 4> color = GhostPreviewStyle::Ignored;
        switch (request.State.Mode)
        {
        case SmartBrushMode::Add:
            affected = !occupied;
            state = affected ? GhostVoxelState::Added : GhostVoxelState::Ignored;
            color = affected ? GhostPreviewStyle::Added : GhostPreviewStyle::Ignored;
            break;
        case SmartBrushMode::Erase:
            affected = occupied;
            state = affected ? GhostVoxelState::Erased : GhostVoxelState::Ignored;
            color = affected ? GhostPreviewStyle::Erased : GhostPreviewStyle::Ignored;
            break;
        case SmartBrushMode::Paint:
            affected = voxel && voxel->PaletteIndex != request.State.PaletteIndex;
            state = affected ? GhostVoxelState::Painted : GhostVoxelState::Ignored;
            color = affected ? request.ActivePaletteColor : GhostPreviewStyle::Ignored;
            break;
        case SmartBrushMode::Replace:
        default: break;
        }
        if (affected)
        {
            ++result.Statistics.Affected;
            result.AffectedPositions.push_back(position);
        }
        else
        {
            ++result.Statistics.Ignored;
        }
        AppendGhost(result, position, state, color, alpha);
    }
    return result;
}

SmartBrushPreviewResult SmartBrushPreviewResolver::Resolve(
    const SmartToolPlan& plan,
    const std::array<float, 4>& activePaletteColor, const float requestedAlpha)
{
    static_cast<void>(activePaletteColor);
    SmartBrushPreviewResult result;
    const float alpha = ResolveAlpha(requestedAlpha);
    const SmartBrushResult& brush = plan.BrushResult();
    result.Code = brush.Code;
    result.Error = brush.Error;
    result.RenderPlan = brush.RenderPlan;
    result.Statistics = {brush.Statistics.Total, 0U, 0U, brush.Statistics.Clipped};

    if (brush.Code != SmartBrushResultCode::Valid &&
        brush.Code != SmartBrushResultCode::OutOfBounds)
    {
        AppendGhost(result, plan.Placement().Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }

    result.GhostVoxels.reserve(result.GhostVoxels.size() + plan.Cells().size());
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
            result.AffectedPositions.push_back(cell.Position);
        }
        else if (state != GhostVoxelState::Clipped) ++result.Statistics.Ignored;
        AppendGhost(result, cell.Position, state, color, alpha);
    }
    return result;
}

const SmartBrushPreviewResult& SmartBrushPreviewCache::Resolve(
    const SmartBrushPreviewCacheKey& key, const SmartBrushPreviewRequest& request)
{
    if (!key_ || *key_ != key)
    {
        result_ = SmartBrushPreviewResolver::Resolve(request);
        key_ = key;
        ++resolutionCount_;
    }
    return result_;
}

void SmartBrushPreviewCache::Clear() noexcept
{
    key_.reset();
    result_ = {};
}

std::size_t SmartBrushPreviewCache::ResolutionCount() const noexcept
{
    return resolutionCount_;
}
} // namespace VoxelForge::Editor
