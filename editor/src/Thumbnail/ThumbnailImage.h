#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

struct ThumbnailImage final
{
    std::uint32_t Width = 0U;
    std::uint32_t Height = 0U;
    std::vector<std::uint8_t> Pixels;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool operator==(const ThumbnailImage&) const noexcept = default;
};

[[nodiscard]] bool WriteThumbnailImage(
    const std::filesystem::path& path,
    const ThumbnailImage& image,
    std::string& errorMessage);
[[nodiscard]] bool ReadThumbnailImage(
    const std::filesystem::path& path,
    ThumbnailImage& image,
    std::string& errorMessage);

} // namespace VoxelForge::Editor
