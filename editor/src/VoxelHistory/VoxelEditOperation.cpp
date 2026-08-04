#include "VoxelEditOperation.h"

#include <limits>

namespace VoxelForge::Editor
{

std::size_t EstimateVoxelEditOperationMemory(
    const VoxelEditOperation& operation) noexcept
{
    constexpr std::size_t FixedSize = sizeof(VoxelEditOperation);
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (operation.Label.size() > maximum - FixedSize)
        return maximum;
    std::size_t result = FixedSize + operation.Label.size();
    const auto addVector = [&result, maximum](
        const std::size_t count, const std::size_t elementSize) noexcept
    {
        if (count > (maximum - result) / elementSize) return false;
        result += count * elementSize;
        return true;
    };
    if (!addVector(operation.Changes.size(), sizeof(VoxelChange)))
        return maximum;
    if (operation.SelectionTransition)
    {
        if (result > maximum - sizeof(VoxelEditSelectionTransition))
            return maximum;
        result += sizeof(VoxelEditSelectionTransition);
        if (!addVector(operation.SelectionTransition->Before.Voxels.size(),
                sizeof(Asset::Voxel::VoxelPosition)) ||
            !addVector(operation.SelectionTransition->After.Voxels.size(),
                sizeof(Asset::Voxel::VoxelPosition)))
            return maximum;
    }
    if (operation.PaletteChange)
    {
        if (result > maximum - sizeof(VoxelPaletteChange))
            return maximum;
        result += sizeof(VoxelPaletteChange);
    }
    if (operation.StampVariantMetadata)
    {
        if (result > maximum - sizeof(VoxelEditStampVariantMetadata))
            return maximum;
        result += sizeof(VoxelEditStampVariantMetadata);
        if (operation.StampVariantMetadata->ExpectedContentHash.size() >
            maximum - result)
            return maximum;
        result +=
            operation.StampVariantMetadata->ExpectedContentHash.size();
    }
    return result;
}

} // namespace VoxelForge::Editor
