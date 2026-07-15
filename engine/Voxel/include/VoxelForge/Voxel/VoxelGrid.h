#pragma once

#include "Voxel.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VoxelForge::Voxel
{

class VoxelGrid final
{
public:
    // Dense v1 grids are capped at 32 MiB of voxel payload.
    static constexpr std::size_t MaximumVoxelCount =
        256U * 256U * 256U;

    [[nodiscard]] bool Resize(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t depth);
    void Clear() noexcept;
    void Fill(Voxel voxel) noexcept;

    // Pointers returned by Get() are invalidated by a successful Resize().
    [[nodiscard]] const Voxel* Get(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z) const noexcept;
    [[nodiscard]] bool Set(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z,
        Voxel voxel) noexcept;
    [[nodiscard]] bool IsInside(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z) const noexcept;

    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] std::uint32_t Depth() const noexcept;
    [[nodiscard]] std::size_t VoxelCount() const noexcept;
    [[nodiscard]] std::size_t OccupiedVoxelCount() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] const std::vector<Voxel>& Data() const noexcept;

private:
    [[nodiscard]] std::size_t Index(
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z) const noexcept;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t depth_ = 0;
    std::vector<Voxel> voxels_;
    std::size_t occupiedVoxelCount_ = 0;
};

} // namespace VoxelForge::Voxel
