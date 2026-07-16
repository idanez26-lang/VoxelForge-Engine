#pragma once

#include "VoxelForge/Asset/Vox/VoxModelAnalyzer.h"
#include "Thumbnail/ThumbnailMetadata.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace VoxelForge::Editor
{

struct ModelAssetMetadata final
{
    std::uint32_t FormatVersion = 1U;
    std::string AssetId;
    std::string AssetType = "voxel_model";
    std::string SourceFile;
    std::string SourceExtension = ".vox";
    std::string Importer = "vox";
    std::uint32_t ImporterVersion = 1U;
    std::uintmax_t FileSize = 0U;
    std::int64_t SourceModifiedTime = 0;
    std::optional<Asset::Vox::VoxModelAnalysis> Analysis;
    std::optional<ThumbnailMetadata> Thumbnail;
};

enum class MetadataEnsureStatus
{
    Created,
    Unchanged,
    Repaired,
    Failed
};

struct MetadataOperationResult final
{
    MetadataEnsureStatus Status = MetadataEnsureStatus::Failed;
    ModelAssetMetadata Metadata;
    std::string Message;

    [[nodiscard]] bool Succeeded() const noexcept;
};

struct MetadataReadResult final
{
    bool Succeeded = false;
    ModelAssetMetadata Metadata;
    std::string Message;
};

struct MetadataRebuildReport final
{
    std::size_t Created = 0U;
    std::size_t Unchanged = 0U;
    std::size_t Repaired = 0U;
    std::size_t Ignored = 0U;
    std::size_t Errors = 0U;
    std::size_t AnalysesCreated = 0U;
    std::size_t AnalysesUpdated = 0U;
    std::size_t AnalysesUnchanged = 0U;
    std::vector<std::string> ErrorMessages;
};

enum class MetadataAnalysisStatus
{
    Created,
    Updated,
    Unchanged,
    Failed
};

struct MetadataAnalysisResult final
{
    MetadataAnalysisStatus Status = MetadataAnalysisStatus::Failed;
    MetadataEnsureStatus MetadataStatus = MetadataEnsureStatus::Failed;
    ModelAssetMetadata Metadata;
    Asset::Vox::VoxModelAnalysis Analysis;
    std::string Message;

    [[nodiscard]] bool Succeeded() const noexcept;
};

class ModelAssetMetadataService final
{
public:
    using AssetIdGenerator = std::function<std::string()>;
    using BeforeInstallCallback = std::function<bool()>;

    explicit ModelAssetMetadataService(
        AssetIdGenerator assetIdGenerator = {},
        BeforeInstallCallback beforeInstall = {});

    [[nodiscard]] bool SetModelsDirectory(
        const std::filesystem::path& modelsDirectory);
    void ClearModelsDirectory() noexcept;

    [[nodiscard]] MetadataOperationResult CreateMetadata(
        const std::filesystem::path& modelPath);
    [[nodiscard]] MetadataReadResult ReadMetadata(
        const std::filesystem::path& metadataPath) const;
    [[nodiscard]] bool WriteMetadata(
        const std::filesystem::path& modelPath,
        const ModelAssetMetadata& metadata,
        std::string& errorMessage) const;
    [[nodiscard]] bool ValidateMetadata(
        const ModelAssetMetadata& metadata,
        const std::filesystem::path& modelPath,
        std::string& errorMessage) const;
    [[nodiscard]] MetadataOperationResult EnsureMetadata(
        const std::filesystem::path& modelPath);
    [[nodiscard]] Asset::Vox::VoxModelAnalysis Analyze(
        const std::filesystem::path& modelPath) const;
    [[nodiscard]] MetadataAnalysisResult AnalyzeAndUpdateMetadata(
        const std::filesystem::path& modelPath,
        bool forceReanalysis = false);
    [[nodiscard]] std::optional<Asset::Vox::VoxModelAnalysis>
        ReadCachedAnalysis(const std::filesystem::path& modelPath) const;
    [[nodiscard]] bool NeedsReanalysis(
        const std::filesystem::path& modelPath,
        const ModelAssetMetadata& metadata) const;
    [[nodiscard]] MetadataRebuildReport RebuildMetadata();

    [[nodiscard]] std::filesystem::path MetadataPathFor(
        const std::filesystem::path& modelPath) const;
    [[nodiscard]] const std::filesystem::path& ModelsDirectory() const noexcept;
    [[nodiscard]] static bool IsMetadataFile(
        const std::filesystem::path& path) noexcept;
    [[nodiscard]] static bool IsValidAssetId(const std::string& assetId) noexcept;

private:
    [[nodiscard]] bool ResolveModelPath(
        const std::filesystem::path& modelPath,
        std::filesystem::path& resolvedPath,
        std::string& errorMessage) const;
    [[nodiscard]] ModelAssetMetadata BuildMetadata(
        const std::filesystem::path& modelPath,
        std::string assetId,
        std::string& errorMessage) const;
    [[nodiscard]] std::string GenerateAssetId() const;

    std::filesystem::path modelsDirectory_;
    AssetIdGenerator assetIdGenerator_;
    BeforeInstallCallback beforeInstall_;
    mutable std::unordered_set<std::string> issuedAssetIds_;
};

} // namespace VoxelForge::Editor
