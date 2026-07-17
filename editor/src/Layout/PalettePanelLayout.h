#pragma once

#include <cstddef>

namespace VoxelForge::Editor
{

struct PaletteGridLayout final
{
    std::size_t ColumnCount = 1U;
    float SwatchSize = 1.0F;

    [[nodiscard]] bool IsValid() const noexcept;
};

[[nodiscard]] PaletteGridLayout CalculatePaletteGridLayout(
    float availableWidth,
    float itemSpacing,
    float minimumSwatchSize,
    std::size_t itemCount) noexcept;

} // namespace VoxelForge::Editor
