#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoxelForge::Asset::Vox
{

namespace
{
constexpr std::array<std::uint8_t, 6> CubeLevels = {
    255U, 204U, 153U, 102U, 51U, 0U};
constexpr std::array<std::uint8_t, 10> RampLevels = {
    238U, 221U, 187U, 170U, 136U,
    119U, 85U, 68U, 34U, 17U};

constexpr std::array<VoxColor, VoxPaletteSize> BuildDefaultPalette()
{
    std::array<VoxColor, VoxPaletteSize> palette{};
    std::size_t index = 1U;

    // This is the canonical MagicaVoxel palette from the official VOX
    // specification: a 6x6x6 RGB cube without black, followed by red,
    // green, blue and gray ramps. Palette index 0 is reserved for empty
    // space and stays transparent; voxels use indices 1 through 255.
    for (const std::uint8_t red : CubeLevels)
    {
        for (const std::uint8_t green : CubeLevels)
        {
            for (const std::uint8_t blue : CubeLevels)
            {
                if (red == 0U && green == 0U && blue == 0U)
                {
                    continue;
                }

                palette[index++] = {red, green, blue, 255U};
            }
        }
    }

    for (const std::uint8_t level : RampLevels)
    {
        palette[index++] = {level, 0U, 0U, 255U};
    }

    for (const std::uint8_t level : RampLevels)
    {
        palette[index++] = {0U, level, 0U, 255U};
    }

    for (const std::uint8_t level : RampLevels)
    {
        palette[index++] = {0U, 0U, level, 255U};
    }

    for (const std::uint8_t level : RampLevels)
    {
        palette[index++] = {level, level, level, 255U};
    }

    return palette;
}

constexpr auto DefaultPalette = BuildDefaultPalette();
static_assert(DefaultPalette[0] == VoxColor{0U, 0U, 0U, 0U});
static_assert(DefaultPalette[1] == VoxColor{255U, 255U, 255U, 255U});
static_assert(DefaultPalette[215] == VoxColor{0U, 0U, 51U, 255U});
static_assert(DefaultPalette[216] == VoxColor{238U, 0U, 0U, 255U});
static_assert(DefaultPalette[255] == VoxColor{17U, 17U, 17U, 255U});
}

const std::array<VoxColor, VoxPaletteSize>& DefaultVoxPalette() noexcept
{
    return DefaultPalette;
}

} // namespace VoxelForge::Asset::Vox
