#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include <algorithm>
#include <array>
#include <new>
#include <vector>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] bool ResolveColor(const VoxelStamp& stamp,
    const std::uint8_t localColor, Asset::Vox::VoxColor& color) noexcept
{
    const auto found = std::find_if(
        stamp.Palette().begin(),
        stamp.Palette().end(),
        [localColor](const StampPaletteEntry& entry)
        {
            return entry.LocalColorId == localColor;
        });
    if (found == stamp.Palette().end())
    {
        return false;
    }

    color = found->Color;
    return true;
}

} // namespace

VoxelPreviewData StampLivePreviewBuilder::Build(
    const StampLivePreviewRequest& request) noexcept
{
    VoxelPreviewData result{};
    try
    {
        if (request.Stamp == nullptr || request.Stamp->Voxels().empty())
        {
            return result;
        }

        const VoxelStamp& stamp = *request.Stamp;
        if (request.Document != nullptr &&
            request.Document->GetModel(request.SubModelIndex) == nullptr)
        {
            return result;
        }

        std::vector<VoxelPreviewSourceVoxel> sources;
        sources.reserve(stamp.Voxels().size());
        for (const StampVoxel& source : stamp.Voxels())
        {
            Asset::Vox::VoxColor color{};
            if (!ResolveColor(stamp, source.LocalColorId, color))
            {
                return result;
            }

            sources.push_back({{source.Position.X, source.Position.Y, source.Position.Z}, color});
        }
        result = VoxelPreviewBuilder::Build({
            .SourceId = stamp.Identity().Id,
            .SourceRevision = stamp.Identity().ContentHash,
            .Bounds = {
                {stamp.Bounds().Minimum.X, stamp.Bounds().Minimum.Y, stamp.Bounds().Minimum.Z},
                {stamp.Bounds().Maximum.X, stamp.Bounds().Maximum.Y, stamp.Bounds().Maximum.Z}},
            .Pivot = {
                {stamp.Pivot().LocalPosition.X, stamp.Pivot().LocalPosition.Y, stamp.Pivot().LocalPosition.Z},
                stamp.Pivot().LocalNormal.X,
                stamp.Pivot().LocalNormal.Y,
                stamp.Pivot().LocalNormal.Z},
            .TargetPivot = {request.TargetPivot.X, request.TargetPivot.Y, request.TargetPivot.Z},
            .Voxels = sources,
            .ForceInvalid = request.ForceInvalid});
        if (!result.IsActive())
        {
            return result;
        }

        for (VoxelPreviewVoxel& voxel : result.Voxels)
        {
            if (request.Document != nullptr &&
                request.Document->GetVoxel(voxel.Position, request.SubModelIndex))
            {
                voxel.OverlapsExisting = true;
                if (result.State != VoxelPreviewState::Invalid)
                {
                    result.State = VoxelPreviewState::Overlap;
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        result = {};
    }
    return result;
}

} // namespace VoxelForge::Editor::Stamps
