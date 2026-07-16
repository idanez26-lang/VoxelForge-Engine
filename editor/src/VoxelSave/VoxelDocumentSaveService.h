#pragma once

#include "ModelImport/ModelAssetMetadataService.h"
#include "Thumbnail/VoxThumbnailService.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace VoxelForge::Editor
{

enum class VoxelDocumentSaveStage
{
    Idle,
    Validating,
    WritingTemporary,
    VerifyingTemporary,
    BackingUp,
    Replacing,
    VerifyingFinal,
    UpdatingMetadata,
    GeneratingThumbnail,
    Completed,
    Failed
};

enum class VoxelDocumentSaveStatus
{
    Succeeded,
    SucceededWithWarning,
    Failed,
    Busy
};

struct VoxelDocumentSaveResult final
{
    VoxelDocumentSaveStatus Status = VoxelDocumentSaveStatus::Failed;
    VoxelDocumentSaveStage Stage = VoxelDocumentSaveStage::Idle;
    std::filesystem::path SavedPath;
    std::string Message;
    std::string Warning;
    bool MetadataUpdated = false;
    bool ThumbnailUpdated = false;
    bool AssetBrowserRefreshed = false;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Status == VoxelDocumentSaveStatus::Succeeded ||
            Status == VoxelDocumentSaveStatus::SucceededWithWarning;
    }
};

class IVoxelSaveFileSystem
{
public:
    virtual ~IVoxelSaveFileSystem() = default;

    [[nodiscard]] virtual bool Inspect(
        const std::filesystem::path& path,
        bool& exists,
        bool& regularFile,
        bool& symbolicLink,
        std::string& error) const = 0;
    [[nodiscard]] virtual bool WriteAndFlush(
        const std::filesystem::path& path,
        std::span<const std::uint8_t> bytes,
        std::string& error) = 0;
    [[nodiscard]] virtual bool FileSize(
        const std::filesystem::path& path,
        std::uintmax_t& size,
        std::string& error) const = 0;
    [[nodiscard]] virtual bool Rename(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string& error) = 0;
    [[nodiscard]] virtual bool RemoveFile(
        const std::filesystem::path& path,
        std::string& error) = 0;
};

[[nodiscard]] std::shared_ptr<IVoxelSaveFileSystem>
CreateStandardVoxelSaveFileSystem();

class VoxelDocumentSaveService final
{
public:
    using RefreshCallback = std::function<void()>;
    using ThumbnailInvalidationCallback = std::function<void()>;

    explicit VoxelDocumentSaveService(
        std::shared_ptr<IVoxelSaveFileSystem> fileSystem = {},
        std::shared_ptr<IVoxThumbnailRenderer> thumbnailRenderer = {});

    [[nodiscard]] bool SetProjectRoot(
        const std::filesystem::path& projectRoot);
    void ClearProject() noexcept;
    void SetRefreshCallback(RefreshCallback callback);
    void SetThumbnailInvalidationCallback(
        ThumbnailInvalidationCallback callback);

    [[nodiscard]] VoxelDocumentSaveResult Save(
        Asset::Voxel::VoxelDocument& document,
        VoxelEditHistory& history);

    [[nodiscard]] bool IsBusy() const noexcept;
    [[nodiscard]] VoxelDocumentSaveStage Stage() const noexcept;
    [[nodiscard]] const VoxelDocumentSaveResult& LastResult() const noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] std::chrono::system_clock::time_point LastSaveTime()
        const noexcept;

    [[nodiscard]] static std::filesystem::path TemporaryPathFor(
        const std::filesystem::path& sourcePath);
    [[nodiscard]] static std::filesystem::path BackupPathFor(
        const std::filesystem::path& sourcePath);

private:
    [[nodiscard]] bool ValidatePath(
        const Asset::Voxel::VoxelDocument& document,
        std::filesystem::path& resolved,
        std::string& error) const;
    [[nodiscard]] bool VerifyFile(
        const std::filesystem::path& path,
        const Asset::Voxel::VoxelDocument& expected,
        std::string& error) const;
    [[nodiscard]] bool RestoreBackup(
        const std::filesystem::path& source,
        const std::filesystem::path& backup,
        const std::filesystem::path& temporary,
        std::string& error);
    VoxelDocumentSaveResult Fail(
        VoxelDocumentSaveStage failedStage,
        std::filesystem::path path,
        std::string message,
        VoxelDocumentSaveStatus status = VoxelDocumentSaveStatus::Failed);

    std::filesystem::path projectRoot_;
    std::filesystem::path modelsDirectory_;
    std::shared_ptr<IVoxelSaveFileSystem> fileSystem_;
    ModelAssetMetadataService metadataService_;
    VoxThumbnailService thumbnailService_;
    RefreshCallback refreshCallback_;
    ThumbnailInvalidationCallback thumbnailInvalidationCallback_;
    VoxelDocumentSaveResult lastResult_{};
    std::chrono::system_clock::time_point lastSaveTime_{};
    VoxelDocumentSaveStage stage_ = VoxelDocumentSaveStage::Idle;
    bool busy_ = false;
};

[[nodiscard]] const char* VoxelDocumentSaveStageName(
    VoxelDocumentSaveStage stage) noexcept;

} // namespace VoxelForge::Editor
