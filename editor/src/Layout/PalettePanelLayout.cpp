#include "Layout/PalettePanelLayout.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{

bool PaletteGridLayout::IsValid() const noexcept
{
    return ColumnCount > 0U && std::isfinite(SwatchSize) && SwatchSize > 0.0F;
}

PaletteGridLayout CalculatePaletteGridLayout(
    const float availableWidth,
    const float itemSpacing,
    const float minimumSwatchSize,
    const std::size_t itemCount) noexcept
{
    const float width = std::isfinite(availableWidth)
        ? std::max(1.0F, availableWidth) : 1.0F;
    const float spacing = std::isfinite(itemSpacing)
        ? std::max(0.0F, itemSpacing) : 0.0F;
    const float minimumSize = std::isfinite(minimumSwatchSize)
        ? std::max(1.0F, minimumSwatchSize) : 1.0F;
    const std::size_t maximumColumns = std::max<std::size_t>(1U, itemCount);
    const float denominator = minimumSize + spacing;
    const auto fittingColumns = static_cast<std::size_t>(std::max(
        1.0F, std::floor((width + spacing) / denominator)));
    const std::size_t columns = std::min(fittingColumns, maximumColumns);
    const float occupiedSpacing =
        spacing * static_cast<float>(columns - 1U);
    const float swatchSize = std::max(
        1.0F,
        (width - occupiedSpacing) / static_cast<float>(columns));
    return {columns, swatchSize};
}

} // namespace VoxelForge::Editor
