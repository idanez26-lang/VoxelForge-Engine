#pragma once

#include <cstdint>
#include <type_traits>

namespace VoxelForge::Voxel
{

struct VoxelColor final
{
    std::uint8_t Red = 0;
    std::uint8_t Green = 0;
    std::uint8_t Blue = 0;
    std::uint8_t Alpha = 0;

    [[nodiscard]] bool operator==(const VoxelColor&) const noexcept = default;
};

static_assert(sizeof(VoxelColor) == 4U);
static_assert(std::is_trivially_copyable_v<VoxelColor>);

} // namespace VoxelForge::Voxel
