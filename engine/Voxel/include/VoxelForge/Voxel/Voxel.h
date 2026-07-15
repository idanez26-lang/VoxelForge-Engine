#pragma once

#include <cstdint>
#include <type_traits>

namespace VoxelForge::Voxel
{

struct Voxel final
{
    // Only bit 0 has meaning in v1. The other flag bits are reserved.
    static constexpr std::uint8_t OccupiedFlag = 1U << 0U;

    // Stable empty value: palette index 0, with every flag cleared.
    std::uint8_t ColorIndex = 0;
    std::uint8_t Flags = 0;

    [[nodiscard]] bool IsOccupied() const noexcept
    {
        return (Flags & OccupiedFlag) != 0U;
    }

    void SetOccupied(const bool occupied) noexcept
    {
        if (occupied)
        {
            Flags |= OccupiedFlag;
        }
        else
        {
            Flags &= static_cast<std::uint8_t>(~OccupiedFlag);
        }
    }

    [[nodiscard]] bool operator==(const Voxel&) const noexcept = default;
};

static_assert(sizeof(Voxel) == 2U);
static_assert(std::is_trivially_copyable_v<Voxel>);

} // namespace VoxelForge::Voxel
