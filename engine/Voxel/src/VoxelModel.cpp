#include "VoxelForge/Voxel/VoxelModel.h"

#include <limits>
#include <utility>

namespace VoxelForge::Voxel
{

const std::string& VoxelModel::Name() const noexcept
{
    return name_;
}

void VoxelModel::SetName(const std::string_view name)
{
    name_ = name;
}

VoxelPalette& VoxelModel::Palette() noexcept
{
    return palette_;
}

const VoxelPalette& VoxelModel::Palette() const noexcept
{
    return palette_;
}

const std::vector<VoxelGrid>& VoxelModel::Grids() const noexcept
{
    return grids_;
}

VoxelGrid* VoxelModel::GetGrid(const std::size_t index) noexcept
{
    return index < grids_.size() ? &grids_[index] : nullptr;
}

const VoxelGrid* VoxelModel::GetGrid(const std::size_t index) const noexcept
{
    return index < grids_.size() ? &grids_[index] : nullptr;
}

void VoxelModel::AddGrid(VoxelGrid grid)
{
    grids_.push_back(std::move(grid));
}

bool VoxelModel::RemoveGrid(const std::size_t index)
{
    if (index >= grids_.size())
    {
        return false;
    }

    grids_.erase(grids_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::size_t VoxelModel::GridCount() const noexcept
{
    return grids_.size();
}

void VoxelModel::Clear() noexcept
{
    name_.clear();
    palette_.Reset();
    grids_.clear();
}

std::uint64_t VoxelModel::TotalVoxelCount() const noexcept
{
    std::uint64_t total = 0U;

    for (const VoxelGrid& grid : grids_)
    {
        const std::uint64_t count = grid.VoxelCount();

        if (count > std::numeric_limits<std::uint64_t>::max() - total)
        {
            return std::numeric_limits<std::uint64_t>::max();
        }

        total += count;
    }

    return total;
}

std::uint64_t VoxelModel::TotalOccupiedVoxelCount() const noexcept
{
    std::uint64_t total = 0U;

    for (const VoxelGrid& grid : grids_)
    {
        const std::uint64_t count = grid.OccupiedVoxelCount();

        if (count > std::numeric_limits<std::uint64_t>::max() - total)
        {
            return std::numeric_limits<std::uint64_t>::max();
        }

        total += count;
    }

    return total;
}

} // namespace VoxelForge::Voxel
