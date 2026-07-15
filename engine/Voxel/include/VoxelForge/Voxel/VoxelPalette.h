#pragma once

#include "VoxelColor.h"

#include <array>
#include <cstddef>

namespace VoxelForge::Voxel
{

class VoxelPalette final
{
public:
    static constexpr std::size_t ColorCount = 256U;

    VoxelPalette() = default;

    [[nodiscard]] const VoxelColor* Get(std::size_t index) const noexcept;
    [[nodiscard]] bool Set(std::size_t index, VoxelColor color) noexcept;
    void Reset() noexcept;

    [[nodiscard]] static constexpr std::size_t Size() noexcept
    {
        return ColorCount;
    }

    [[nodiscard]] const std::array<VoxelColor, ColorCount>& Data() const noexcept;

private:
    // The internal default is deliberately format-neutral: transparent black.
    std::array<VoxelColor, ColorCount> colors_{};
};

static_assert(sizeof(VoxelPalette) == 256U * sizeof(VoxelColor));

} // namespace VoxelForge::Voxel
