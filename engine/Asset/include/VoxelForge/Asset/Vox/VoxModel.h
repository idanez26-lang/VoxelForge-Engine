#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace VoxelForge::Asset::Vox
{

struct VoxColor final
{
    std::uint8_t Red = 0;
    std::uint8_t Green = 0;
    std::uint8_t Blue = 0;
    std::uint8_t Alpha = 0;

    [[nodiscard]] bool operator==(const VoxColor&) const noexcept = default;
};

struct VoxDimensions final
{
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Z = 0;

    [[nodiscard]] bool operator==(const VoxDimensions&) const noexcept = default;
};

struct VoxVoxel final
{
    std::uint8_t X = 0;
    std::uint8_t Y = 0;
    std::uint8_t Z = 0;
    std::uint8_t ColorIndex = 0;

    [[nodiscard]] bool operator==(const VoxVoxel&) const noexcept = default;
};

static_assert(sizeof(VoxVoxel) == 4U);
static_assert(std::is_trivially_copyable_v<VoxVoxel>);

struct VoxModelMetadata final
{
    VoxDimensions Dimensions;
    std::vector<VoxVoxel> Voxels;

    [[nodiscard]] std::size_t VoxelCount() const noexcept
    {
        return Voxels.size();
    }

    [[nodiscard]] bool operator==(
        const VoxModelMetadata&) const noexcept = default;
};

struct VoxModel final
{
    std::uint32_t Version = 0;
    std::vector<VoxModelMetadata> Models;
    std::array<VoxColor, 256> Palette{};
    bool HasCustomPalette = false;
    bool HasPackChunk = false;
    std::uint32_t DeclaredModelCount = 1;
    std::uint32_t ParsedChunkCount = 0;
    std::uint32_t IgnoredChunkCount = 0;

    [[nodiscard]] std::uint64_t TotalVoxelCount() const noexcept
    {
        std::uint64_t total = 0;

        for (const VoxModelMetadata& model : Models)
        {
            total += model.VoxelCount();
        }

        return total;
    }
};

} // namespace VoxelForge::Asset::Vox
