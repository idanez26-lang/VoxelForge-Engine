#include "VoxelBrush.h"

namespace VoxelForge::Editor
{

bool IsVoxelBrushSizeValid(const int size) noexcept
{
    return size >= 1 && size <= MaximumVoxelBrushSize;
}

bool UsesAggregateBrushPreview(const std::size_t voxelCount) noexcept
{
    return voxelCount > MaximumDetailedBrushPreviewVoxelCount;
}

VoxelBrushPreviewRenderMode SelectVoxelBrushPreviewRenderMode(
    const VoxelBrushShape shape,
    const std::size_t voxelCount) noexcept
{
    if (!UsesAggregateBrushPreview(voxelCount))
        return VoxelBrushPreviewRenderMode::DetailedCells;
    return shape == VoxelBrushShape::Sphere
        ? VoxelBrushPreviewRenderMode::AggregateSphere
        : VoxelBrushPreviewRenderMode::AggregateBox;
}

Asset::Voxel::VoxelPosition OffsetVoxelBrushAnchor(
    const Asset::Voxel::VoxelPosition placementTarget,
    const Asset::Voxel::VoxelPosition placementNormal,
    const int size) noexcept
{
    const int depthOffset = IsVoxelBrushSizeValid(size)
        ? (size - 1) / 2 : 0;
    return {
        placementTarget.X + placementNormal.X * depthOffset,
        placementTarget.Y + placementNormal.Y * depthOffset,
        placementTarget.Z + placementNormal.Z * depthOffset};
}

std::vector<Asset::Voxel::VoxelPosition> GenerateVoxelBrush(
    const Asset::Voxel::VoxelPosition anchor,
    const VoxelBrushShape shape,
    const int size)
{
    if (!IsVoxelBrushSizeValid(size) ||
        (shape != VoxelBrushShape::Cube && shape != VoxelBrushShape::Sphere))
    {
        return {};
    }

    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    const int sphereRadiusSquared = size * size;
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(static_cast<std::size_t>(size) *
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int z = minimumOffset; z <= maximumOffset; ++z)
    {
        for (int y = minimumOffset; y <= maximumOffset; ++y)
        {
            for (int x = minimumOffset; x <= maximumOffset; ++x)
            {
                if (shape == VoxelBrushShape::Sphere)
                {
                    // Doubled coordinates keep odd spheres centred on the
                    // anchor and even spheres centred between the central pair.
                    const int dx = 2 * x - (size % 2 == 0 ? 1 : 0);
                    const int dy = 2 * y - (size % 2 == 0 ? 1 : 0);
                    const int dz = 2 * z - (size % 2 == 0 ? 1 : 0);
                    if (dx * dx + dy * dy + dz * dz > sphereRadiusSquared)
                        continue;
                }
                positions.push_back({anchor.X + x, anchor.Y + y, anchor.Z + z});
            }
        }
    }
    return positions;
}

std::size_t EstimateVoxelBrushVoxelCount(
    const VoxelBrushShape shape,
    const int size)
{
    if (!IsVoxelBrushSizeValid(size)) return 0U;
    if (shape == VoxelBrushShape::Cube)
    {
        const std::size_t dimension = static_cast<std::size_t>(size);
        return dimension * dimension * dimension;
    }

    if (shape != VoxelBrushShape::Sphere) return 0U;
    std::size_t count = 0U;
    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    const int radiusSquared = size * size;
    const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
    for (int z = minimumOffset; z <= maximumOffset; ++z)
    {
        for (int y = minimumOffset; y <= maximumOffset; ++y)
        {
            for (int x = minimumOffset; x <= maximumOffset; ++x)
            {
                const int dx = 2 * x - evenCenterOffset;
                const int dy = 2 * y - evenCenterOffset;
                const int dz = 2 * z - evenCenterOffset;
                if (dx * dx + dy * dy + dz * dz <= radiusSquared) ++count;
            }
        }
    }
    return count;
}

} // namespace VoxelForge::Editor
