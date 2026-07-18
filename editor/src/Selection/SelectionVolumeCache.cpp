#include "SelectionVolumeCache.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
[[nodiscard]] bool PositionLess(
    const Asset::Voxel::VoxelPosition& left,
    const Asset::Voxel::VoxelPosition& right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}
}

bool SelectionVolumeCache::SourceCurrent(
    const std::uint64_t documentGeneration,
    const std::uint64_t documentRevision) const noexcept
{
    return sourceValid_ && documentGeneration_ == documentGeneration &&
        documentRevision_ == documentRevision;
}

void SelectionVolumeCache::UpdateSource(
    std::vector<Asset::Voxel::VoxelPosition> existingVoxels,
    const std::uint64_t documentGeneration,
    const std::uint64_t documentRevision)
{
    std::sort(existingVoxels.begin(), existingVoxels.end(), PositionLess);
    existingVoxels.erase(
        std::unique(existingVoxels.begin(), existingVoxels.end()),
        existingVoxels.end());
    sourceVoxels_ = std::move(existingVoxels);
    documentGeneration_ = documentGeneration;
    documentRevision_ = documentRevision;
    sourceValid_ = true;
    resultValid_ = false;
    ++metrics_.SourceRefreshCount;
}

SelectionVolumeEvaluation SelectionVolumeCache::Evaluate(
    const SelectionBounds bounds)
{
    if (!sourceValid_ || !bounds.Valid) return {};
    if (resultValid_ && cachedBounds_ == bounds)
    {
        ++metrics_.BoundsCacheHitCount;
        return {containedVoxels_, false};
    }

    containedVoxels_.clear();
    if (containedVoxels_.capacity() < sourceVoxels_.size())
    {
        containedVoxels_.reserve(sourceVoxels_.size());
        ++metrics_.ResultCapacityGrowthCount;
    }
    for (const Asset::Voxel::VoxelPosition position : sourceVoxels_)
        if (bounds.Contains(position)) containedVoxels_.push_back(position);
    metrics_.VisitedVoxelCount += sourceVoxels_.size();
    ++metrics_.BoundsEvaluationCount;
    cachedBounds_ = bounds;
    resultValid_ = true;
    return {containedVoxels_, true};
}

void SelectionVolumeCache::Clear() noexcept
{
    std::vector<Asset::Voxel::VoxelPosition>().swap(sourceVoxels_);
    std::vector<Asset::Voxel::VoxelPosition>().swap(containedVoxels_);
    cachedBounds_ = {};
    documentGeneration_ = 0U;
    documentRevision_ = 0U;
    sourceValid_ = false;
    resultValid_ = false;
    metrics_ = {};
}

const SelectionVolumeCacheMetrics&
SelectionVolumeCache::Metrics() const noexcept
{
    return metrics_;
}

} // namespace VoxelForge::Editor
