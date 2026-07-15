#include "VoxelForge/Voxel/VoxelGrid.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace VoxelForge::Voxel
{

bool VoxelGrid::Resize(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t depth)
{
    if (width == 0U || height == 0U || depth == 0U)
    {
        width_ = 0U;
        height_ = 0U;
        depth_ = 0U;
        std::vector<Voxel>{}.swap(voxels_);
        occupiedVoxelCount_ = 0U;
        return true;
    }

    constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
    const std::size_t widthValue = width;
    const std::size_t heightValue = height;
    const std::size_t depthValue = depth;

    if (widthValue > maximum / heightValue)
    {
        return false;
    }

    const std::size_t planeSize = widthValue * heightValue;

    if (planeSize > maximum / depthValue)
    {
        return false;
    }

    const std::size_t voxelCount = planeSize * depthValue;

    if (voxelCount > MaximumVoxelCount)
    {
        return false;
    }

    try
    {
        std::vector<Voxel> replacement(voxelCount);
        voxels_ = std::move(replacement);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }

    width_ = width;
    height_ = height;
    depth_ = depth;
    occupiedVoxelCount_ = 0U;
    return true;
}

void VoxelGrid::Clear() noexcept
{
    std::fill(voxels_.begin(), voxels_.end(), Voxel{});
    occupiedVoxelCount_ = 0U;
}

void VoxelGrid::Fill(const Voxel voxel) noexcept
{
    std::fill(voxels_.begin(), voxels_.end(), voxel);
    occupiedVoxelCount_ = voxel.IsOccupied() ? voxels_.size() : 0U;
}

const Voxel* VoxelGrid::Get(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) const noexcept
{
    return IsInside(x, y, z) ? &voxels_[Index(x, y, z)] : nullptr;
}

bool VoxelGrid::Set(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const Voxel voxel) noexcept
{
    if (!IsInside(x, y, z))
    {
        return false;
    }

    Voxel& destination = voxels_[Index(x, y, z)];

    if (destination.IsOccupied() != voxel.IsOccupied())
    {
        if (voxel.IsOccupied())
        {
            ++occupiedVoxelCount_;
        }
        else
        {
            --occupiedVoxelCount_;
        }
    }

    destination = voxel;
    return true;
}

bool VoxelGrid::IsInside(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) const noexcept
{
    return x < width_ && y < height_ && z < depth_;
}

std::uint32_t VoxelGrid::Width() const noexcept
{
    return width_;
}

std::uint32_t VoxelGrid::Height() const noexcept
{
    return height_;
}

std::uint32_t VoxelGrid::Depth() const noexcept
{
    return depth_;
}

std::size_t VoxelGrid::VoxelCount() const noexcept
{
    return voxels_.size();
}

std::size_t VoxelGrid::OccupiedVoxelCount() const noexcept
{
    return occupiedVoxelCount_;
}

bool VoxelGrid::Empty() const noexcept
{
    return voxels_.empty();
}

const std::vector<Voxel>& VoxelGrid::Data() const noexcept
{
    return voxels_;
}

std::size_t VoxelGrid::Index(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z) const noexcept
{
    return static_cast<std::size_t>(x) +
        static_cast<std::size_t>(y) * width_ +
        static_cast<std::size_t>(z) * width_ * height_;
}

} // namespace VoxelForge::Voxel
