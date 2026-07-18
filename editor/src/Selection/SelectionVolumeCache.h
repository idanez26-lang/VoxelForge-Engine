#pragma once

#include "SelectionService.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace VoxelForge::Editor
{

struct SelectionVolumeCacheMetrics final
{
    std::size_t SourceRefreshCount = 0U;
    std::size_t BoundsEvaluationCount = 0U;
    std::size_t BoundsCacheHitCount = 0U;
    std::size_t VisitedVoxelCount = 0U;
    std::size_t ResultCapacityGrowthCount = 0U;
};

struct SelectionVolumeEvaluation final
{
    std::span<const Asset::Voxel::VoxelPosition> Voxels;
    bool Recalculated = false;
};

class SelectionVolumeCache final
{
public:
    [[nodiscard]] bool SourceCurrent(
        std::uint64_t documentGeneration,
        std::uint64_t documentRevision) const noexcept;
    void UpdateSource(
        std::vector<Asset::Voxel::VoxelPosition> existingVoxels,
        std::uint64_t documentGeneration,
        std::uint64_t documentRevision);
    [[nodiscard]] SelectionVolumeEvaluation Evaluate(
        SelectionBounds bounds);
    void Clear() noexcept;

    [[nodiscard]] const SelectionVolumeCacheMetrics& Metrics() const noexcept;

private:
    std::vector<Asset::Voxel::VoxelPosition> sourceVoxels_;
    std::vector<Asset::Voxel::VoxelPosition> containedVoxels_;
    SelectionBounds cachedBounds_{};
    std::uint64_t documentGeneration_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    bool sourceValid_ = false;
    bool resultValid_ = false;
    SelectionVolumeCacheMetrics metrics_{};
};

} // namespace VoxelForge::Editor
