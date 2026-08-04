#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include <new>

namespace VoxelForge::Editor::Stamps
{

VoxelPreviewData StampLivePreviewBuilder::Build(
    const StampPlacementPlan& plan) noexcept
{
    VoxelPreviewData result{};
    try
    {
        if (plan.Stamp.Id.Value() == 0U || plan.Voxels.empty() ||
            !plan.WorldBounds.Valid)
        {
            return result;
        }

        result.SourceId = plan.Stamp.Id;
        result.SourceRevision = plan.Stamp.ContentHash;
        result.LocalBounds = {
            {plan.LocalBounds.Minimum.X, plan.LocalBounds.Minimum.Y,
                plan.LocalBounds.Minimum.Z},
            {plan.LocalBounds.Maximum.X, plan.LocalBounds.Maximum.Y,
                plan.LocalBounds.Maximum.Z}};
        result.WorldBounds = {
            plan.WorldBounds.Minimum, plan.WorldBounds.Maximum};
        result.Pivot = {
            {plan.Pivot.LocalPosition.X, plan.Pivot.LocalPosition.Y,
                plan.Pivot.LocalPosition.Z},
            plan.Pivot.LocalNormal.X,
            plan.Pivot.LocalNormal.Y,
            plan.Pivot.LocalNormal.Z};
        result.Transform = {
            {plan.Transform.TargetPivot.X, plan.Transform.TargetPivot.Y,
                plan.Transform.TargetPivot.Z},
            plan.Transform.QuarterTurns,
            static_cast<std::uint8_t>(plan.Transform.Mirror)};
        result.State = plan.HasErrors()
            ? VoxelPreviewState::Invalid
            : plan.Statistics.OverlapCount != 0U
            ? VoxelPreviewState::Overlap
            : VoxelPreviewState::Valid;
        result.Voxels.reserve(plan.Voxels.size());
        for (const StampPlannedVoxel& voxel : plan.Voxels)
        {
            result.Voxels.push_back({
                voxel.WorldPosition, voxel.Color, voxel.Overlap});
        }
        result.Placement = BuildVoxelPlacementPreview(result);
    }
    catch (const std::bad_alloc&)
    {
        result = {};
    }
    return result;
}

} // namespace VoxelForge::Editor::Stamps
