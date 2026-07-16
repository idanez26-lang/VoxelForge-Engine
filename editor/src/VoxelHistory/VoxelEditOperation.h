#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

using VoxelChange = Asset::Voxel::VoxelDocumentChange;

struct VoxelEditOperation final
{
    std::string Label;
    std::vector<VoxelChange> Changes;
};

[[nodiscard]] std::size_t EstimateVoxelEditOperationMemory(
    const VoxelEditOperation& operation) noexcept;

} // namespace VoxelForge::Editor
