#pragma once

#include "VoxelSelection/VoxelRay.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <optional>

namespace VoxelForge::Editor
{

[[nodiscard]] std::optional<Asset::Voxel::VoxelPosition>
FindVoxelConstructionPlaneTarget(
    const Asset::Voxel::VoxelDocument& document,
    std::size_t subModelIndex,
    const VoxelRay& worldRay,
    Vec3 modelCenter) noexcept;

} // namespace VoxelForge::Editor
