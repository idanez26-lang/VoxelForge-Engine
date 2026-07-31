#include "VoxelDocumentSaveService.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>
#include "EditorPathCompare.h"

namespace VoxelForge::Editor
{
namespace
{
std::string LowerExtension(const std::filesystem::path& path)
{
    std::string result = path.extension().string();
    std::transform(result.begin(), result.end(), result.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return result;
}

class StandardVoxelSaveFileSystem final : public IVoxelSaveFileSystem
{
public:
    bool Inspect(
        const std::filesystem::path& path,
        bool& exists,
        bool& regularFile,
        bool& symbolicLink,
        std::string& error) const override
    {
        error.clear();
        exists = false;
        regularFile = false;
        symbolicLink = false;
        std::error_code filesystemError;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(path, filesystemError);
        if (filesystemError)
        {
            if (filesystemError == std::errc::no_such_file_or_directory)
                return true;
            error = "Unable to inspect path: " + filesystemError.message();
            return false;
        }
        exists = std::filesystem::exists(status);
        symbolicLink = std::filesystem::is_symlink(status);
        regularFile = std::filesystem::is_regular_file(status);
        return true;
    }

    bool WriteAndFlush(
        const std::filesystem::path& path,
        const std::span<const std::uint8_t> bytes,
        std::string& error) override
    {
        error.clear();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "Unable to create VOX temporary file.";
            return false;
        }
        if (!bytes.empty())
        {
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }
        if (!output)
        {
            error = "Unable to write VOX temporary file.";
            output.close();
            return false;
        }
        output.flush();
        if (!output)
        {
            error = "Unable to flush VOX temporary file.";
            output.close();
            return false;
        }
        output.close();
        if (!output)
        {
            error = "Unable to close VOX temporary file safely.";
            return false;
        }
        return true;
    }

    bool FileSize(
        const std::filesystem::path& path,
        std::uintmax_t& size,
        std::string& error) const override
    {
        std::error_code filesystemError;
        size = std::filesystem::file_size(path, filesystemError);
        if (!filesystemError) return true;
        error = "Unable to read file size: " + filesystemError.message();
        return false;
    }

    bool Rename(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string& error) override
    {
        std::error_code filesystemError;
        std::filesystem::rename(source, destination, filesystemError);
        if (!filesystemError) return true;
        error = "Unable to rename file: " + filesystemError.message();
        return false;
    }

    bool RemoveFile(
        const std::filesystem::path& path,
        std::string& error) override
    {
        bool exists = false;
        bool regular = false;
        bool symlink = false;
        if (!Inspect(path, exists, regular, symlink, error)) return false;
        if (!exists) return true;
        if (!regular || symlink)
        {
            error = "Refusing to remove a non-regular transaction file.";
            return false;
        }
        std::error_code filesystemError;
        if (std::filesystem::remove(path, filesystemError) && !filesystemError)
            return true;
        error = "Unable to remove transaction file: " +
            filesystemError.message();
        return false;
    }
};

class BusyGuard final
{
public:
    explicit BusyGuard(bool& busy) noexcept : busy_(busy) { busy_ = true; }
    ~BusyGuard() { busy_ = false; }
private:
    bool& busy_;
};
}

std::shared_ptr<IVoxelSaveFileSystem> CreateStandardVoxelSaveFileSystem()
{
    return std::make_shared<StandardVoxelSaveFileSystem>();
}

VoxelDocumentSaveService::VoxelDocumentSaveService(
    std::shared_ptr<IVoxelSaveFileSystem> fileSystem,
    std::shared_ptr<IVoxThumbnailRenderer> thumbnailRenderer)
    : fileSystem_(fileSystem ? std::move(fileSystem) :
          CreateStandardVoxelSaveFileSystem()),
      thumbnailService_(std::move(thumbnailRenderer))
{
}

bool VoxelDocumentSaveService::SetProjectRoot(
    const std::filesystem::path& projectRoot)
{
    ClearProject();
    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
        return false;
    const std::filesystem::path models = canonical / "Assets" / "Models";
    const auto modelsStatus = std::filesystem::symlink_status(models, error);
    if (error || std::filesystem::is_symlink(modelsStatus) ||
        !std::filesystem::is_directory(modelsStatus))
        return false;
    projectRoot_ = canonical;
    modelsDirectory_ = std::filesystem::weakly_canonical(models, error);
    if (error || !metadataService_.SetModelsDirectory(modelsDirectory_) ||
        !thumbnailService_.SetProjectRoot(projectRoot_))
    {
        ClearProject();
        return false;
    }
    stage_ = VoxelDocumentSaveStage::Idle;
    return true;
}

void VoxelDocumentSaveService::ClearProject() noexcept
{
    projectRoot_.clear();
    modelsDirectory_.clear();
    metadataService_.ClearModelsDirectory();
    thumbnailService_.ClearProject();
    lastResult_ = {};
    stage_ = VoxelDocumentSaveStage::Idle;
    busy_ = false;
}

void VoxelDocumentSaveService::SetRefreshCallback(RefreshCallback callback)
{
    refreshCallback_ = std::move(callback);
}

void VoxelDocumentSaveService::SetThumbnailInvalidationCallback(
    ThumbnailInvalidationCallback callback)
{
    thumbnailInvalidationCallback_ = std::move(callback);
}

VoxelDocumentSaveResult VoxelDocumentSaveService::Save(
    Asset::Voxel::VoxelDocument& document,
    VoxelEditHistory& history)
{
    if (busy_)
        return Fail(VoxelDocumentSaveStage::Failed, document.SourcePath(),
            "A VOX save transaction is already active.",
            VoxelDocumentSaveStatus::Busy);
    BusyGuard guard(busy_);
    const std::uint64_t revisionBeforeSave = document.GetRevision();
    stage_ = VoxelDocumentSaveStage::Validating;
    std::filesystem::path source;
    std::string error;
    if (!ValidatePath(document, source, error))
        return Fail(stage_, document.SourcePath(), std::move(error));

    const std::filesystem::path temporary = TemporaryPathFor(source);
    const std::filesystem::path backup = BackupPathFor(source);
    for (const std::filesystem::path& transactionPath : {temporary, backup})
    {
        bool exists = false;
        bool regular = false;
        bool symlink = false;
        if (!fileSystem_->Inspect(
                transactionPath, exists, regular, symlink, error))
            return Fail(stage_, source, std::move(error));
        if (exists)
            return Fail(stage_, source,
                "A recognized VOX save transaction file already exists: " +
                transactionPath.filename().string() +
                ". It was left untouched for manual recovery.");
    }

    const Asset::Voxel::VoxDocumentWriteResult serialized =
        Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    if (!serialized.Succeeded())
        return Fail(stage_, source, serialized.Message);

    stage_ = VoxelDocumentSaveStage::WritingTemporary;
    if (!fileSystem_->WriteAndFlush(temporary, serialized.Bytes, error))
    {
        std::string cleanupError;
        static_cast<void>(fileSystem_->RemoveFile(temporary, cleanupError));
        return Fail(stage_, source, std::move(error));
    }
    std::uintmax_t temporarySize = 0U;
    if (!fileSystem_->FileSize(temporary, temporarySize, error) ||
        temporarySize != serialized.Bytes.size())
    {
        std::string cleanupError;
        static_cast<void>(fileSystem_->RemoveFile(temporary, cleanupError));
        return Fail(stage_, source, error.empty()
            ? "VOX temporary file size differs from serialized content."
            : std::move(error));
    }

    stage_ = VoxelDocumentSaveStage::VerifyingTemporary;
    if (!VerifyFile(temporary, document, error))
    {
        std::string cleanupError;
        static_cast<void>(fileSystem_->RemoveFile(temporary, cleanupError));
        return Fail(stage_, source, std::move(error));
    }

    stage_ = VoxelDocumentSaveStage::BackingUp;
    if (!fileSystem_->Rename(source, backup, error))
    {
        std::string cleanupError;
        static_cast<void>(fileSystem_->RemoveFile(temporary, cleanupError));
        return Fail(stage_, source, "Unable to back up original VOX: " + error);
    }

    stage_ = VoxelDocumentSaveStage::Replacing;
    if (!fileSystem_->Rename(temporary, source, error))
    {
        std::string rollbackError;
        const bool restored = RestoreBackup(
            source, backup, temporary, rollbackError);
        return Fail(stage_, source,
            "Unable to replace original VOX: " + error +
            (restored ? " Original restored."
                      : " Rollback failed: " + rollbackError));
    }

    stage_ = VoxelDocumentSaveStage::VerifyingFinal;
    if (!VerifyFile(source, document, error))
    {
        std::string rollbackError;
        const bool restored = RestoreBackup(
            source, backup, temporary, rollbackError);
        return Fail(stage_, source,
            "Final VOX validation failed: " + error +
            (restored ? " Original restored."
                      : " Rollback failed: " + rollbackError));
    }

    stage_ = VoxelDocumentSaveStage::UpdatingMetadata;
    MetadataAnalysisResult metadata =
        metadataService_.AnalyzeAndUpdateMetadata(source, true);
    if (!metadata.Succeeded())
        metadata = metadataService_.AnalyzeAndUpdateMetadata(source, true);
    if (!metadata.Succeeded())
    {
        std::string rollbackError;
        const bool restored = RestoreBackup(
            source, backup, temporary, rollbackError);
        if (restored)
            static_cast<void>(
                metadataService_.AnalyzeAndUpdateMetadata(source, true));
        return Fail(stage_, source,
            "VOX metadata update failed: " + metadata.Message +
            (restored ? " Original restored."
                      : " Rollback failed: " + rollbackError));
    }

    stage_ = VoxelDocumentSaveStage::GeneratingThumbnail;
    const ThumbnailGenerationResult thumbnail =
        thumbnailService_.Generate(source, true);
    const bool thumbnailSucceeded = thumbnail.Succeeded();
    const std::string warning = thumbnailSucceeded
        ? std::string{}
        : "Thumbnail generation failed: " + thumbnail.Message;

    if (!fileSystem_->RemoveFile(backup, error))
        return Fail(stage_, source,
            "VOX saved, but transaction backup cleanup failed: " + error);

    if (thumbnailInvalidationCallback_) thumbnailInvalidationCallback_();
    if (refreshCallback_) refreshCallback_();
    history.MarkSavedState(document);
    if (document.GetRevision() != revisionBeforeSave)
        return Fail(stage_, source,
            "VOX save unexpectedly changed the document revision.");

    stage_ = VoxelDocumentSaveStage::Completed;
    lastSaveTime_ = std::chrono::system_clock::now();
    lastResult_ = {
        thumbnailSucceeded ? VoxelDocumentSaveStatus::Succeeded
                           : VoxelDocumentSaveStatus::SucceededWithWarning,
        stage_, source,
        thumbnailSucceeded ? "VOX document saved."
                           : "VOX document saved with a thumbnail warning.",
        warning, true, thumbnailSucceeded, true};
    return lastResult_;
}

bool VoxelDocumentSaveService::IsBusy() const noexcept { return busy_; }
VoxelDocumentSaveStage VoxelDocumentSaveService::Stage() const noexcept
{
    return stage_;
}
const VoxelDocumentSaveResult& VoxelDocumentSaveService::LastResult()
    const noexcept
{
    return lastResult_;
}
const std::filesystem::path& VoxelDocumentSaveService::ProjectRoot()
    const noexcept
{
    return projectRoot_;
}
std::chrono::system_clock::time_point
VoxelDocumentSaveService::LastSaveTime() const noexcept
{
    return lastSaveTime_;
}

std::filesystem::path VoxelDocumentSaveService::TemporaryPathFor(
    const std::filesystem::path& sourcePath)
{
    return std::filesystem::path(sourcePath.string() + ".vfsave.tmp");
}

std::filesystem::path VoxelDocumentSaveService::BackupPathFor(
    const std::filesystem::path& sourcePath)
{
    return std::filesystem::path(sourcePath.string() + ".vfsave.bak");
}

bool VoxelDocumentSaveService::ValidatePath(
    const Asset::Voxel::VoxelDocument& document,
    std::filesystem::path& resolved,
    std::string& error) const
{
    if (projectRoot_.empty() || modelsDirectory_.empty())
    {
        error = "No project is configured for VOX saving.";
        return false;
    }
    if (document.SourcePath().empty())
    {
        error = "Active VOX document has no source path.";
        return false;
    }
    if (LowerExtension(document.SourcePath()) != ".vox")
    {
        error = "Only internal .vox documents can be saved.";
        return false;
    }
    std::error_code filesystemError;
    const std::filesystem::path absolute =
        std::filesystem::absolute(document.SourcePath(), filesystemError)
            .lexically_normal();
    if (filesystemError ||
        !IsSameDirectoryAsCanonical(absolute.parent_path(), modelsDirectory_))
    {
        error = "VOX save path must be a direct child of Assets/Models.";
        return false;
    }
    bool exists = false;
    bool regular = false;
    bool symlink = false;
    if (!fileSystem_->Inspect(
            absolute, exists, regular, symlink, error))
        return false;
    if (!exists || !regular || symlink)
    {
        error = "VOX save source must be an existing regular, non-symbolic file.";
        return false;
    }
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(absolute, filesystemError);
    if (filesystemError || canonical.parent_path() != modelsDirectory_)
    {
        error = "VOX save path escapes Assets/Models.";
        return false;
    }
    resolved = canonical;
    return true;
}

bool VoxelDocumentSaveService::VerifyFile(
    const std::filesystem::path& path,
    const Asset::Voxel::VoxelDocument& expected,
    std::string& error) const
{
    const Asset::Voxel::VoxDocumentLoadResult loaded =
        Asset::Voxel::VoxDocumentLoader{}.Load(path, expected.AssetId());
    if (!loaded.Succeeded())
    {
        error = loaded.Message.empty()
            ? "Saved VOX cannot be reloaded." : loaded.Message;
        return false;
    }
    return Asset::Voxel::AreVoxelDocumentsEquivalent(
        expected, *loaded.Document, error);
}

bool VoxelDocumentSaveService::RestoreBackup(
    const std::filesystem::path& source,
    const std::filesystem::path& backup,
    const std::filesystem::path& temporary,
    std::string& error)
{
    error.clear();
    bool exists = false;
    bool regular = false;
    bool symlink = false;
    std::string operationError;
    if (fileSystem_->Inspect(source, exists, regular, symlink, operationError) &&
        exists && regular && !symlink &&
        !fileSystem_->RemoveFile(source, operationError))
    {
        error = operationError;
        return false;
    }
    if (!fileSystem_->Rename(backup, source, operationError))
    {
        error = operationError;
        return false;
    }
    std::string cleanupError;
    static_cast<void>(fileSystem_->RemoveFile(temporary, cleanupError));
    return true;
}

VoxelDocumentSaveResult VoxelDocumentSaveService::Fail(
    const VoxelDocumentSaveStage failedStage,
    std::filesystem::path path,
    std::string message,
    const VoxelDocumentSaveStatus status)
{
    stage_ = VoxelDocumentSaveStage::Failed;
    lastResult_ = {
        status, failedStage, std::move(path), std::move(message), {},
        false, false, false};
    return lastResult_;
}

const char* VoxelDocumentSaveStageName(
    const VoxelDocumentSaveStage stage) noexcept
{
    switch (stage)
    {
    case VoxelDocumentSaveStage::Idle: return "Idle";
    case VoxelDocumentSaveStage::Validating: return "Validating";
    case VoxelDocumentSaveStage::WritingTemporary: return "Writing temporary";
    case VoxelDocumentSaveStage::VerifyingTemporary: return "Verifying temporary";
    case VoxelDocumentSaveStage::BackingUp: return "Backing up";
    case VoxelDocumentSaveStage::Replacing: return "Replacing";
    case VoxelDocumentSaveStage::VerifyingFinal: return "Verifying final";
    case VoxelDocumentSaveStage::UpdatingMetadata: return "Updating metadata";
    case VoxelDocumentSaveStage::GeneratingThumbnail: return "Generating thumbnail";
    case VoxelDocumentSaveStage::Completed: return "Completed";
    case VoxelDocumentSaveStage::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
