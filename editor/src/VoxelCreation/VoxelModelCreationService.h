#pragma once

#include "ModelImport/ModelAssetMetadataService.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <filesystem>
#include <functional>
#include <string>

namespace VoxelForge::Editor
{

enum class VoxelModelCreationCollisionAction
{
    Ask,
    Rename,
    Replace,
    Cancel
};

enum class VoxelModelCreationStatus
{
    Created,
    Renamed,
    Replaced,
    Collision,
    Cancelled,
    Failed
};

struct VoxelModelCreationRequest final
{
    std::string Name;
    Asset::Voxel::VoxelDimensions Dimensions{64U, 64U, 64U};
};

struct VoxelModelCreationStepResult final
{
    bool Succeeded = false;
    std::string Message;
};

struct VoxelModelCreationResult final
{
    VoxelModelCreationStatus Status = VoxelModelCreationStatus::Failed;
    std::filesystem::path ModelPath;
    ModelAssetMetadata Metadata;
    std::string Message;
    std::string Warning;
    bool ThumbnailGenerated = false;
    bool AssetBrowserRefreshed = false;
    bool Opened = false;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Status == VoxelModelCreationStatus::Created ||
            Status == VoxelModelCreationStatus::Renamed ||
            Status == VoxelModelCreationStatus::Replaced;
    }
};

class VoxelModelCreationService final
{
public:
    using ThumbnailCallback = std::function<VoxelModelCreationStepResult(
        const std::filesystem::path&)>;
    using AssetBrowserCallback = std::function<bool(
        const std::filesystem::path&)>;
    using OpenCallback = std::function<bool(
        const std::filesystem::path&)>;

    explicit VoxelModelCreationService(
        ModelAssetMetadataService::AssetIdGenerator assetIdGenerator = {});

    [[nodiscard]] bool SetProjectRoot(
        const std::filesystem::path& projectRoot);
    void ClearProject() noexcept;
    void SetThumbnailCallback(ThumbnailCallback callback);
    void SetAssetBrowserCallback(AssetBrowserCallback callback);
    void SetOpenCallback(OpenCallback callback);

    [[nodiscard]] VoxelModelCreationResult CreateModel(
        const VoxelModelCreationRequest& request,
        VoxelModelCreationCollisionAction collisionAction =
            VoxelModelCreationCollisionAction::Ask);

    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] const std::filesystem::path& ModelsDirectory() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] static bool ValidateModelName(
        const std::string& name,
        std::string& error);
    [[nodiscard]] static bool ValidateDimensions(
        const Asset::Voxel::VoxelDimensions& dimensions,
        std::string& error);

private:
    [[nodiscard]] std::filesystem::path NextAvailablePath(
        const std::filesystem::path& desired) const;
    VoxelModelCreationResult Fail(
        std::filesystem::path path,
        std::string message);

    std::filesystem::path projectRoot_;
    std::filesystem::path modelsDirectory_;
    std::string lastError_;
    ModelAssetMetadataService metadataService_;
    ThumbnailCallback thumbnailCallback_;
    AssetBrowserCallback assetBrowserCallback_;
    OpenCallback openCallback_;
    bool busy_ = false;
};

[[nodiscard]] const char* VoxelModelCreationStatusName(
    VoxelModelCreationStatus status) noexcept;

} // namespace VoxelForge::Editor
