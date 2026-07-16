#pragma once

#include "ThumbnailMetadata.h"
#include "VoxThumbnailRenderer.h"

#include "ModelImport/ModelAssetMetadataService.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace VoxelForge::Editor
{

enum class ThumbnailGenerationStatus
{
    Generated,
    Unchanged,
    Failed
};

struct ThumbnailGenerationResult final
{
    ThumbnailGenerationStatus Status = ThumbnailGenerationStatus::Failed;
    std::filesystem::path ModelPath;
    std::filesystem::path CachePath;
    ThumbnailMetadata Metadata;
    std::string Message;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Status != ThumbnailGenerationStatus::Failed;
    }
};

struct ThumbnailRebuildReport final
{
    std::size_t Generated = 0U;
    std::size_t Unchanged = 0U;
    std::size_t Failed = 0U;
    std::size_t Skipped = 0U;
    std::size_t RemovedOrphans = 0U;
    std::vector<std::string> Errors;
};

class VoxThumbnailService final
{
public:
    explicit VoxThumbnailService(
        std::shared_ptr<IVoxThumbnailRenderer> renderer = {});

    [[nodiscard]] bool SetProjectRoot(
        const std::filesystem::path& projectRoot);
    void ClearProject() noexcept;

    [[nodiscard]] std::filesystem::path CachePathForAssetId(
        const std::string& assetId) const;
    [[nodiscard]] ThumbnailPresentation Describe(
        const std::filesystem::path& modelPath) const;
    [[nodiscard]] bool NeedsRegeneration(
        const std::filesystem::path& modelPath,
        const ModelAssetMetadata& metadata) const;
    [[nodiscard]] ThumbnailGenerationResult Generate(
        const std::filesystem::path& modelPath,
        bool forceRegeneration = false);
    [[nodiscard]] bool RemoveForModel(
        const std::filesystem::path& modelPath,
        std::string& errorMessage);
    [[nodiscard]] bool RemoveByAssetId(
        const std::string& assetId,
        std::string& errorMessage);
    [[nodiscard]] ThumbnailRebuildReport Rebuild();

    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] const std::filesystem::path& CacheDirectory() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] static bool IsRecognizedCacheFile(
        const std::filesystem::path& path) noexcept;

private:
    [[nodiscard]] bool EnsureCacheDirectory(std::string& errorMessage) const;
    [[nodiscard]] bool IsSafeCachePath(
        const std::filesystem::path& path,
        std::string& errorMessage) const;
    [[nodiscard]] bool InstallImage(
        const std::filesystem::path& cachePath,
        const ThumbnailImage& image,
        std::string& errorMessage) const;
    [[nodiscard]] ThumbnailMetadata BuildThumbnailMetadata(
        const ModelAssetMetadata& metadata,
        ThumbnailStatus status,
        std::filesystem::path cachePath,
        std::string errorMessage = {}) const;
    void RemoveOrphans(
        const std::unordered_set<std::string>& liveAssetIds,
        ThumbnailRebuildReport& report) const;

    std::filesystem::path projectRoot_;
    std::filesystem::path modelsDirectory_;
    std::filesystem::path cacheDirectory_;
    std::shared_ptr<IVoxThumbnailRenderer> renderer_;
    mutable std::string lastError_;
    ModelAssetMetadataService metadataService_;
};

} // namespace VoxelForge::Editor
