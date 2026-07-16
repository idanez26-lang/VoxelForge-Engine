#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace VoxelForge::Asset::Vox
{

struct VoxSubModelAnalysis final
{
    std::uint32_t SizeX = 0U;
    std::uint32_t SizeY = 0U;
    std::uint32_t SizeZ = 0U;
    std::uint64_t VoxelCount = 0U;

    [[nodiscard]] bool operator==(
        const VoxSubModelAnalysis&) const noexcept = default;
};

struct VoxModelAnalysis final
{
    bool Valid = false;
    std::uint32_t FormatVersion = 0U;
    std::uint32_t ModelCount = 0U;
    std::uint32_t SizeX = 0U;
    std::uint32_t SizeY = 0U;
    std::uint32_t SizeZ = 0U;
    std::uint64_t VoxelCount = 0U;
    std::uint32_t UsedPaletteColorCount = 0U;
    bool HasCustomPalette = false;
    std::uint64_t FileSize = 0U;
    std::vector<VoxSubModelAnalysis> Models;
    std::string Error;

    [[nodiscard]] bool operator==(
        const VoxModelAnalysis&) const noexcept = default;
};

class VoxModelAnalyzer final
{
public:
    [[nodiscard]] VoxModelAnalysis Analyze(
        const std::filesystem::path& filePath) const;
};

} // namespace VoxelForge::Asset::Vox
