#pragma once

#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Voxel/Voxel.h"

#include <cstdint>

namespace VoxelForge::Editor
{

[[nodiscard]] CommandResult ReadEditableVoxel(
    VoxelEditSession& session,
    std::uint64_t modelGeneration,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    Voxel::Voxel& voxel) noexcept;

[[nodiscard]] CommandResult ApplyVoxelEdit(
    VoxelEditSession& session,
    std::uint64_t modelGeneration,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    Voxel::Voxel expected,
    Voxel::Voxel replacement);

} // namespace VoxelForge::Editor
