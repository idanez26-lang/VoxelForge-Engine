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

    // VF-STAB-01 bugs 6-7: palette index 0 means "no voxel", never a colour.
    // VoxelDocument rejects it for any present voxel ("palette indices between
    // 1 and 255"), so accepting it here produced an editor selection the domain
    // would always refuse — the Add or Paint then failed silently. The valid
    // range is the domain's range, enforced at the source of the choice.
    [[nodiscard]] static constexpr std::uint8_t MinimumIndex() noexcept
    {
        return 1U;
    }

    [[nodiscard]] bool SetIndex(const std::size_t index) noexcept
    {
        if (index < MinimumIndex() || index >= Voxel::VoxelPalette::Size())
            return false;
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
    // Starts on the first paintable colour, never on the "no voxel" index.
    std::uint8_t index_ = 1U;
};

} // namespace VoxelForge::Editor
