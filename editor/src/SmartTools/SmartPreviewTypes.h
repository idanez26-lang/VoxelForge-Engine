#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstdint>

namespace VoxelForge::Editor
{
// Renderer-facing value objects shared by the official engine and the legacy
// resolver facade.  They own no rendering or planning behaviour.
enum class GhostVoxelState : std::uint8_t
{
    Added,
    Erased,
    Painted,
    Ignored,
    Invalid,
    Clipped
};

struct GhostVoxel final
{
    Asset::Voxel::VoxelPosition Position{};
    GhostVoxelState State = GhostVoxelState::Invalid;
    std::array<float, 4> Color{};
    float Alpha = 0.5F;
};

struct GhostPreviewStyle final
{
    static constexpr float DefaultAlpha = 0.5F;
    static constexpr std::array<float, 4> Added{0.25F, 0.92F, 0.43F, 1.0F};
    static constexpr std::array<float, 4> Erased{0.95F, 0.26F, 0.28F, 1.0F};
    static constexpr std::array<float, 4> Ignored{1.0F, 0.59F, 0.18F, 1.0F};
    static constexpr std::array<float, 4> Invalid{0.52F, 0.55F, 0.60F, 1.0F};
    static constexpr std::array<float, 4> Clipped{0.45F, 0.08F, 0.10F, 1.0F};
};
} // namespace VoxelForge::Editor
