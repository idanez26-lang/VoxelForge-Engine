#include "VoxelForge/Voxel/VoxelPalette.h"

namespace VoxelForge::Voxel
{

const VoxelColor* VoxelPalette::Get(const std::size_t index) const noexcept
{
    return index < colors_.size() ? &colors_[index] : nullptr;
}

bool VoxelPalette::Set(
    const std::size_t index,
    const VoxelColor color) noexcept
{
    if (index >= colors_.size())
    {
        return false;
    }

    colors_[index] = color;
    return true;
}

void VoxelPalette::Reset() noexcept
{
    colors_.fill({});
}

const std::array<VoxelColor, VoxelPalette::ColorCount>&
VoxelPalette::Data() const noexcept
{
    return colors_;
}

} // namespace VoxelForge::Voxel
