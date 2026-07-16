#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace VoxelForge::Editor
{

inline constexpr std::uint32_t VoxThumbnailWidth = 256U;
inline constexpr std::uint32_t VoxThumbnailHeight = 256U;
inline constexpr std::uint32_t VoxThumbnailGeneratorVersion = 1U;
inline constexpr const char* VoxThumbnailExtension = ".vfthumb";

enum class ThumbnailStatus
{
    Missing,
    Generating,
    Valid,
    Outdated,
    Failed
};

struct ThumbnailMetadata final
{
    ThumbnailStatus Status = ThumbnailStatus::Missing;
    std::string File;
    std::uintmax_t SourceSize = 0U;
    std::int64_t SourceModifiedTime = 0;
    std::uint32_t GeneratorVersion = 0U;
    std::uint32_t Width = 0U;
    std::uint32_t Height = 0U;
    std::uint32_t AnalysisModelCount = 0U;
    std::uint32_t AnalysisSizeX = 0U;
    std::uint32_t AnalysisSizeY = 0U;
    std::uint32_t AnalysisSizeZ = 0U;
    std::uint64_t AnalysisVoxelCount = 0U;
    std::string Error;

    [[nodiscard]] bool operator==(const ThumbnailMetadata&) const noexcept = default;
};

[[nodiscard]] inline const char* ThumbnailStatusName(
    const ThumbnailStatus status) noexcept
{
    switch (status)
    {
    case ThumbnailStatus::Missing: return "missing";
    case ThumbnailStatus::Generating: return "generating";
    case ThumbnailStatus::Valid: return "valid";
    case ThumbnailStatus::Outdated: return "outdated";
    case ThumbnailStatus::Failed: return "failed";
    }
    return "missing";
}

[[nodiscard]] inline ThumbnailStatus ParseThumbnailStatus(
    const std::string& value) noexcept
{
    if (value == "generating") return ThumbnailStatus::Generating;
    if (value == "valid") return ThumbnailStatus::Valid;
    if (value == "outdated") return ThumbnailStatus::Outdated;
    if (value == "failed") return ThumbnailStatus::Failed;
    return ThumbnailStatus::Missing;
}

struct ThumbnailPresentation final
{
    ThumbnailStatus Status = ThumbnailStatus::Missing;
    std::filesystem::path CachedFile;
    std::uint32_t Width = 0U;
    std::uint32_t Height = 0U;
    std::uint32_t GeneratorVersion = 0U;
    std::string Error;

    [[nodiscard]] bool IsReady() const noexcept
    {
        return Status == ThumbnailStatus::Valid && !CachedFile.empty();
    }
};

} // namespace VoxelForge::Editor
