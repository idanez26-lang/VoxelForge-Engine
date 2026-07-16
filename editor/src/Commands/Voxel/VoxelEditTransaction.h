#pragma once

#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include "VoxelForge/Voxel/Voxel.h"

#include <cstdint>
#include <span>

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
    Voxel::Voxel replacement,
    std::size_t modelIndex = 0U);

enum class VoxelChangeDirection
{
    Forward,
    Backward
};

[[nodiscard]] CommandResult ApplyVoxelChanges(
    VoxelEditSession& session,
    std::uint64_t modelGeneration,
    std::span<const VoxelChange> changes,
    VoxelChangeDirection direction);

} // namespace VoxelForge::Editor
