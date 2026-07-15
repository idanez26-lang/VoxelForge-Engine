#pragma once

#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor
{

class PaintPaletteSelection final
{
public:
    [[nodiscard]] std::uint8_t Index() const noexcept
    {
        return index_;
    }

    [[nodiscard]] bool SetIndex(const std::size_t index) noexcept
    {
        if (index >= Voxel::VoxelPalette::Size()) return false;
        index_ = static_cast<std::uint8_t>(index);
        return true;
    }

    [[nodiscard]] const Voxel::VoxelColor* SelectedColor(
        const Voxel::VoxelPalette* palette) const noexcept
    {
        return palette ? palette->Get(index_) : nullptr;
    }

    // The artist's current paint choice belongs to the editor session, not a model.
    void OnModelLoaded() noexcept {}

private:
    std::uint8_t index_ = 0U;
};

} // namespace VoxelForge::Editor
