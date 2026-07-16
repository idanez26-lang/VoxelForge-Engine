#pragma once

#include "ModelAssetMetadataService.h"
#include "Thumbnail/VoxThumbnailService.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class ModelImportFormat
{
    Vox,
    VoxelForgeVoxel,
    Qubicle,
    WavefrontObj,
    Unsupported
};

enum class ModelImportCollisionAction
{
    Ask,
    Replace,
    Rename,
    Skip,
    Cancel
};

enum class ModelImportStatus
{
    Imported,
    Replaced,
    Renamed,
    Skipped,
    Cancelled,
    Collision,
    Unsupported,
    Failed
};

struct ModelImportResult final
{
    ModelImportStatus Status = ModelImportStatus::Failed;
    std::filesystem::path SourcePath;
    std::filesystem::path DestinationPath;
    std::string Message;
    ThumbnailGenerationStatus ThumbnailStatus =
        ThumbnailGenerationStatus::Failed;
    std::string ThumbnailMessage;

    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] bool ChangedAssets() const noexcept;
};

class ModelImportService final
{
public:
    using RefreshCallback = std::function<void()>;

    static constexpr std::size_t MaximumRecentImports = 10U;

    explicit ModelImportService(
        ModelAssetMetadataService::AssetIdGenerator assetIdGenerator = {},
        ModelAssetMetadataService::BeforeInstallCallback beforeInstall = {});
    ModelImportService(
        ModelAssetMetadataService::AssetIdGenerator assetIdGenerator,
        ModelAssetMetadataService::BeforeInstallCallback beforeInstall,
        std::shared_ptr<IVoxThumbnailRenderer> thumbnailRenderer);

    [[nodiscard]] bool SetProjectRoot(
        const std::filesystem::path& projectRoot);
    void ClearProjectRoot() noexcept;
    void SetRefreshCallback(RefreshCallback callback);

    [[nodiscard]] ModelImportResult ImportModel(
        const std::filesystem::path& sourcePath,
        ModelImportCollisionAction collisionAction =
            ModelImportCollisionAction::Ask);
    [[nodiscard]] std::vector<ModelImportResult> ImportModels(
        const std::vector<std::filesystem::path>& sourcePaths,
        ModelImportCollisionAction collisionAction =
            ModelImportCollisionAction::Ask);
    [[nodiscard]] MetadataRebuildReport RebuildMetadata();
    [[nodiscard]] ThumbnailRebuildReport RebuildThumbnails();
    [[nodiscard]] MetadataReadResult ReadMetadataForModel(
        const std::filesystem::path& modelPath) const;
    [[nodiscard]] MetadataAnalysisResult AnalyzeModel(
        const std::filesystem::path& modelPath,
        bool forceReanalysis = false);
    [[nodiscard]] ThumbnailGenerationResult GenerateThumbnail(
        const std::filesystem::path& modelPath,
        bool forceRegeneration = true);

    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] std::filesystem::path ModelsDirectory() const;
    [[nodiscard]] const std::vector<std::filesystem::path>&
        RecentImports() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] static ModelImportFormat ClassifyFormat(
        const std::filesystem::path& path);
    [[nodiscard]] static bool IsFormatEnabled(ModelImportFormat format) noexcept;

private:
    [[nodiscard]] ModelImportResult ImportModelImpl(
        const std::filesystem::path& sourcePath,
        ModelImportCollisionAction collisionAction,
        bool notifyRefresh);
    [[nodiscard]] std::filesystem::path NextAvailablePath(
        const std::filesystem::path& destinationPath) const;
    void RecordRecentImport(const std::filesystem::path& destinationPath);
    void NotifyRefresh() const;
    ModelImportResult Fail(
        ModelImportStatus status,
        std::filesystem::path sourcePath,
        std::filesystem::path destinationPath,
        std::string message);

    std::filesystem::path projectRoot_;
    std::vector<std::filesystem::path> recentImports_;
    RefreshCallback refreshCallback_;
    std::string lastError_;
    ModelAssetMetadataService metadataService_;
    VoxThumbnailService thumbnailService_;
};

} // namespace VoxelForge::Editor
