#include "Preview/VoxelPreview.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::int32_t FixedUnits = 256;

[[nodiscard]] std::array<float, 4U> PreviewColor(
    const Asset::Vox::VoxColor source,
    const VoxelPreviewSemantic semantic) noexcept
{
    const std::array<float, 4U> tint =
        semantic == VoxelPreviewSemantic::Overlap
        ? std::array<float, 4U>{1.0F, 0.50F, 0.12F, 1.0F}
        : semantic == VoxelPreviewSemantic::Invalid
        ? std::array<float, 4U>{0.95F, 0.16F, 0.18F, 1.0F}
        : std::array<float, 4U>{0.22F, 0.90F, 0.38F, 1.0F};
    constexpr float scale = 1.0F / 255.0F;
    const std::array<float, 4U> normalized{
        source.Red * scale,
        source.Green * scale,
        source.Blue * scale,
        source.Alpha * scale};
    std::array<float, 4U> result{};
    for (std::size_t index = 0U; index < 3U; ++index)
        result[index] = normalized[index] * 0.78F + tint[index] * 0.22F;
    result[3] = 1.0F;
    return result;
}

[[nodiscard]] bool MakeGrid(
    const std::int32_t target,
    const std::int32_t local,
    const std::int32_t pivot, std::int32_t& output) noexcept
{
    const std::int64_t fixed = static_cast<std::int64_t>(target) +
        static_cast<std::int64_t>(local) * FixedUnits - pivot;
    if (fixed < std::numeric_limits<std::int32_t>::min() ||
        fixed > std::numeric_limits<std::int32_t>::max() ||
        fixed % FixedUnits != 0)
    {
        return false;
    }

    output = static_cast<std::int32_t>(fixed / FixedUnits);
    return true;
}
}

VoxelPlacementPreview BuildVoxelPlacementPreview(
    const VoxelPreviewData& preview,
    const std::uint64_t revision) noexcept
{
    try
    {
        std::vector<VoxelPreviewInstance> instances;
        instances.reserve(preview.Voxels.size());
        for (const VoxelPreviewVoxel& voxel : preview.Voxels)
        {
            const VoxelPreviewSemantic semantic =
                preview.State == VoxelPreviewState::Invalid
                ? VoxelPreviewSemantic::Invalid
                : voxel.OverlapsExisting
                ? VoxelPreviewSemantic::Overlap
                : VoxelPreviewSemantic::Valid;
            instances.push_back({
                voxel.Position, semantic, PreviewColor(voxel.Color, semantic),
                0.52F});
        }
        return VoxelPlacementPreview::FromInstances(
            revision, std::move(instances));
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
}

VoxelPreviewData VoxelPreviewBuilder::Build(const VoxelPreviewBuildRequest& request) noexcept
{
    VoxelPreviewData result{};
    try
    {
        if (request.SourceId.Value() == 0U || request.Voxels.empty())
        {
            return result;
        }

        result.SourceId = request.SourceId;
        result.SourceRevision = request.SourceRevision;
        result.LocalBounds = request.Bounds;
        result.Pivot = request.Pivot;
        result.Transform.TargetPivot = request.TargetPivot;
        result.State = request.ForceInvalid
            ? VoxelPreviewState::Invalid
            : VoxelPreviewState::Valid;
        result.Voxels.reserve(request.Voxels.size());
        for (const VoxelPreviewSourceVoxel& source : request.Voxels)
        {
            Asset::Voxel::VoxelPosition position{};
            if (!MakeGrid(
                    request.TargetPivot.X,
                    source.LocalPosition.X,
                    request.Pivot.LocalPosition.X,
                    position.X) ||
                !MakeGrid(
                    request.TargetPivot.Y,
                    source.LocalPosition.Y,
                    request.Pivot.LocalPosition.Y,
                    position.Y) ||
                !MakeGrid(
                    request.TargetPivot.Z,
                    source.LocalPosition.Z,
                    request.Pivot.LocalPosition.Z,
                    position.Z))
            {
                result.Voxels.clear();
                result.State = VoxelPreviewState::Invalid;
                return result;
            }

            result.Voxels.push_back({position, source.Color, false});
        }
        result.WorldBounds.Minimum = result.Voxels.front().Position;
        result.WorldBounds.Maximum = result.WorldBounds.Minimum;
        for (const VoxelPreviewVoxel& voxel : result.Voxels)
        {
            result.WorldBounds.Minimum.X = std::min(result.WorldBounds.Minimum.X, voxel.Position.X);
            result.WorldBounds.Minimum.Y = std::min(result.WorldBounds.Minimum.Y, voxel.Position.Y);
            result.WorldBounds.Minimum.Z = std::min(result.WorldBounds.Minimum.Z, voxel.Position.Z);
            result.WorldBounds.Maximum.X = std::max(result.WorldBounds.Maximum.X, voxel.Position.X);
            result.WorldBounds.Maximum.Y = std::max(result.WorldBounds.Maximum.Y, voxel.Position.Y);
            result.WorldBounds.Maximum.Z = std::max(result.WorldBounds.Maximum.Z, voxel.Position.Z);
        }
        result.Placement = BuildVoxelPlacementPreview(result);
    }
    catch (const std::bad_alloc&)
    {
        result = {};
    }
    return result;
}

bool VoxelPreviewSession::Activate(VoxelPreviewData preview)
{
    if (!preview.IsActive())
    {
        return Clear();
    }

    if (preview_ && preview_->SourceId == preview.SourceId &&
        preview_->SourceRevision == preview.SourceRevision &&
        preview_->Voxels == preview.Voxels &&
        preview_->LocalBounds == preview.LocalBounds &&
        preview_->WorldBounds == preview.WorldBounds &&
        preview_->Pivot == preview.Pivot &&
        preview_->Transform == preview.Transform &&
        preview_->State == preview.State)
    {
        return false;
    }

    preview.Revision = ++revision_;
    preview.Placement = preview.Placement.WithRevision(preview.Revision);
    preview_ = std::move(preview);
    return true;
}

bool VoxelPreviewSession::Clear() noexcept
{
    if (!preview_) return false;
    preview_.reset();
    ++revision_;
    return true;
}

const VoxelPreviewData* VoxelPreviewSession::Current() const noexcept
{
    return preview_ ? &*preview_ : nullptr;
}

std::uint64_t VoxelPreviewSession::Revision() const noexcept { return revision_; }

} // namespace VoxelForge::Editor
