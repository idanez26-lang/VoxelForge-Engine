#pragma once

#include "VoxelRay.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Voxel
{
class VoxelGrid;
}

namespace VoxelForge::Editor
{

enum class VoxelHitFace : std::uint8_t
{
    None,
    NegativeX,
    PositiveX,
    NegativeY,
    PositiveY,
    NegativeZ,
    PositiveZ
};

struct VoxelCoordinates final
{
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Z = 0;

    [[nodiscard]] bool operator==(const VoxelCoordinates&) const noexcept = default;
};

struct VoxelRaycastHit final
{
    VoxelCoordinates Coordinates{};
    VoxelHitFace Face = VoxelHitFace::None;
    float Distance = 0.0F;
    Vec3 Impact{};
    std::uint8_t ColorIndex = 0;

    [[nodiscard]] bool operator==(const VoxelRaycastHit&) const noexcept = default;
};

[[nodiscard]] std::optional<VoxelRaycastHit> RaycastVoxelGrid(
    const Voxel::VoxelGrid& grid,
    const VoxelRay& ray) noexcept;

[[nodiscard]] const char* VoxelHitFaceName(VoxelHitFace face) noexcept;

} // namespace VoxelForge::Editor
