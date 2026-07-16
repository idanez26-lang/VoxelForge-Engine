#include "VoxelEditOperation.h"

#include <limits>

namespace VoxelForge::Editor
{

std::size_t EstimateVoxelEditOperationMemory(
    const VoxelEditOperation& operation) noexcept
{
    constexpr std::size_t FixedSize = sizeof(VoxelEditOperation);
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (operation.Changes.size() >
        (maximum - FixedSize - operation.Label.size()) / sizeof(VoxelChange))
    {
        return maximum;
    }
    return FixedSize + operation.Label.size() +
        operation.Changes.size() * sizeof(VoxelChange);
}

} // namespace VoxelForge::Editor
