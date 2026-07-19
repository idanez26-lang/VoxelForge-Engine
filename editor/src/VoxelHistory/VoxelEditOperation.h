#pragma once

#include "Selection/SelectionService.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

using VoxelChange = Asset::Voxel::VoxelDocumentChange;

struct VoxelEditSelectionSnapshot final
{
    std::uint64_t DocumentGeneration = 0U;
    std::vector<Asset::Voxel::VoxelPosition> Voxels;
    SelectionBounds Bounds{};
};

struct VoxelEditSelectionTransition final
{
    VoxelEditSelectionSnapshot Before;
    VoxelEditSelectionSnapshot After;
};

struct VoxelEditOperation final
{
    std::string Label;
    std::vector<VoxelChange> Changes;
    std::shared_ptr<const VoxelEditSelectionTransition> SelectionTransition;
};

[[nodiscard]] std::size_t EstimateVoxelEditOperationMemory(
    const VoxelEditOperation& operation) noexcept;

} // namespace VoxelForge::Editor
