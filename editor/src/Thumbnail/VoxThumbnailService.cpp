#include "VoxThumbnailService.h"

#include "ThumbnailImage.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include "EditorPathCompare.h"

namespace VoxelForge::Editor
{
namespace
{
bool IsWithin(
    const std::filesystem::path& path,
    const std::filesystem::path& root)
{
    const auto mismatch = std::mismatch(
        root.begin(), root.end(), path.begin(), path.end());
    return mismatch.first == root.end();
}

bool IsVoxFile(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return extension == ".vox";
}
}

VoxThumbnailService::VoxThumbnailService(
    std::shared_ptr<IVoxThumbnailRenderer> renderer)
    : renderer_(renderer ? std::move(renderer) :
          CreateDefaultVoxThumbnailRenderer())
{
}

bool VoxThumbnailService::SetProjectRoot(
    const std::filesystem::path& projectRoot)
{
    ClearProject();
    if (projectRoot.empty())
    {
        lastError_ = "Project root cannot be empty.";
        return false;
    }
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(projectRoot, error).lexically_normal();
    if (error || !std::filesystem::is_directory(absolute, error) || error)
    {
        lastError_ = "Project root does not exist or is inaccessible.";
        return false;
    }
    projectRoot_ = absolute;
    modelsDirectory_ = projectRoot_ / "Assets" / "Models";
    cacheDirectory_ = std::filesystem::weakly_canonical(
        projectRoot_ / "Cache" / "Thumbnails", error);
    if (error) cacheDirectory_ = projectRoot_ / "Cache" / "Thumbnails";
    if (!metadataService_.SetModelsDirectory(modelsDirectory_))
    {
        lastError_ = "Unable to configure model metadata for thumbnails.";
        ClearProject();
        return false;
    }
    std::string cacheError;
    if (!EnsureCacheDirectory(cacheError))
    {
        lastError_ = std::move(cacheError);
        ClearProject();
        return false;
    }
    return true;
}

void VoxThumbnailService::ClearProject() noexcept
{
    projectRoot_.clear();
    modelsDirectory_.clear();
    cacheDirectory_.clear();
    lastError_.clear();
    metadataService_.ClearModelsDirectory();
}

std::filesystem::path VoxThumbnailService::CachePathForAssetId(
    const std::string& assetId) const
{
    if (cacheDirectory_.empty() ||
        !ModelAssetMetadataService::IsValidAssetId(assetId))
        return {};
    return cacheDirectory_ / (assetId + VoxThumbnailExtension);
}

ThumbnailPresentation VoxThumbnailService::Describe(
    const std::filesystem::path& modelPath) const
{
    ThumbnailPresentation result;
    if (projectRoot_.empty()) return result;
    const MetadataReadResult metadata = metadataService_.ReadMetadata(
        metadataService_.MetadataPathFor(modelPath));
    if (!metadata.Succeeded || !metadata.Metadata.Thumbnail)
        return result;
    const ThumbnailMetadata& thumbnail = *metadata.Metadata.Thumbnail;
    result.Status = thumbnail.Status;
    result.CachedFile = CachePathForAssetId(metadata.Metadata.AssetId);
    result.Width = thumbnail.Width;
    result.Height = thumbnail.Height;
    result.GeneratorVersion = thumbnail.GeneratorVersion;
    result.Error = thumbnail.Error;
    if (thumbnail.Status == ThumbnailStatus::Valid &&
        NeedsRegeneration(modelPath, metadata.Metadata))
        result.Status = ThumbnailStatus::Outdated;
    return result;
}

bool VoxThumbnailService::NeedsRegeneration(
    const std::filesystem::path& modelPath,
    const ModelAssetMetadata& metadata) const
{
    if (!metadata.Thumbnail || !metadata.Analysis ||
        !metadata.Analysis->Valid ||
        metadata.Thumbnail->Status != ThumbnailStatus::Valid)
        return true;
    const ThumbnailMetadata& thumbnail = *metadata.Thumbnail;
    const auto& analysis = *metadata.Analysis;
    std::error_code sourceError;
    const std::uintmax_t currentSize =
        std::filesystem::file_size(modelPath, sourceError);
    if (sourceError || currentSize != thumbnail.SourceSize)
        return true;
    const auto currentModifiedTime =
        std::filesystem::last_write_time(modelPath, sourceError);
    if (sourceError || static_cast<std::int64_t>(
            currentModifiedTime.time_since_epoch().count()) !=
        thumbnail.SourceModifiedTime)
        return true;
    if (thumbnail.SourceSize != metadata.FileSize ||
        thumbnail.SourceModifiedTime != metadata.SourceModifiedTime ||
        thumbnail.GeneratorVersion != VoxThumbnailGeneratorVersion ||
        thumbnail.Width != VoxThumbnailWidth ||
        thumbnail.Height != VoxThumbnailHeight ||
        thumbnail.AnalysisModelCount != analysis.ModelCount ||
        thumbnail.AnalysisSizeX != analysis.SizeX ||
        thumbnail.AnalysisSizeY != analysis.SizeY ||
        thumbnail.AnalysisSizeZ != analysis.SizeZ ||
        thumbnail.AnalysisVoxelCount != analysis.VoxelCount)
        return true;
    const std::filesystem::path cachePath =
        CachePathForAssetId(metadata.AssetId);
    if (cachePath.empty() || thumbnail.File != cachePath.filename().string())
        return true;
    ThumbnailImage image;
    std::string error;
    return !ReadThumbnailImage(cachePath, image, error) ||
        image.Width != VoxThumbnailWidth || image.Height != VoxThumbnailHeight;
}

ThumbnailGenerationResult VoxThumbnailService::Generate(
    const std::filesystem::path& modelPath,
    const bool forceRegeneration)
{
    lastError_.clear();
    ThumbnailGenerationResult result;
    result.ModelPath = modelPath;
    if (projectRoot_.empty())
    {
        result.Message = "No project is configured for thumbnails.";
        return result;
    }
    const MetadataAnalysisResult analyzed =
        metadataService_.AnalyzeAndUpdateMetadata(modelPath);
    if (!analyzed.Succeeded() || !analyzed.Metadata.Analysis ||
        !analyzed.Metadata.Analysis->Valid)
    {
        result.Message = "VOX analysis is unavailable for thumbnail generation.";
        return result;
    }
    ModelAssetMetadata metadata = analyzed.Metadata;
    result.CachePath = CachePathForAssetId(metadata.AssetId);
    if (result.CachePath.empty())
    {
        result.Message = "Asset ID is invalid for thumbnail cache.";
        return result;
    }
    if (!forceRegeneration && !NeedsRegeneration(modelPath, metadata))
    {
        result.Status = ThumbnailGenerationStatus::Unchanged;
        result.Metadata = *metadata.Thumbnail;
        result.Message = "Thumbnail cache is current.";
        return result;
    }
    const ThumbnailRenderResult rendered = renderer_->Render(modelPath);
    if (!rendered.Succeeded)
    {
        metadata.Thumbnail = BuildThumbnailMetadata(
            metadata, ThumbnailStatus::Failed, result.CachePath,
            rendered.Error);
        std::string metadataError;
        static_cast<void>(metadataService_.WriteMetadata(
            modelPath, metadata, metadataError));
        result.Metadata = *metadata.Thumbnail;
        result.Message = "Thumbnail rendering failed: " + rendered.Error;
        lastError_ = result.Message;
        return result;
    }
    ThumbnailImage previousImage;
    std::string previousImageError;
    const bool hadPreviousImage = ReadThumbnailImage(
        result.CachePath, previousImage, previousImageError);
    std::string installError;
    if (!InstallImage(result.CachePath, rendered.Image, installError))
    {
        metadata.Thumbnail = BuildThumbnailMetadata(
            metadata, ThumbnailStatus::Failed, result.CachePath,
            installError);
        std::string metadataError;
        static_cast<void>(metadataService_.WriteMetadata(
            modelPath, metadata, metadataError));
        result.Metadata = *metadata.Thumbnail;
        result.Message = "Thumbnail installation failed: " + installError;
        lastError_ = result.Message;
        return result;
    }
    const std::optional<ThumbnailMetadata> previous = metadata.Thumbnail;
    metadata.Thumbnail = BuildThumbnailMetadata(
        metadata, ThumbnailStatus::Valid, result.CachePath);
    std::string metadataError;
    if (!metadataService_.WriteMetadata(modelPath, metadata, metadataError))
    {
        std::string restoreError;
        if (hadPreviousImage)
            static_cast<void>(InstallImage(
                result.CachePath, previousImage, restoreError));
        else
            static_cast<void>(RemoveByAssetId(
                metadata.AssetId, restoreError));
        metadata.Thumbnail = previous;
        result.Message = "Thumbnail metadata update failed: " + metadataError;
        lastError_ = result.Message;
        return result;
    }
    result.Status = ThumbnailGenerationStatus::Generated;
    result.Metadata = *metadata.Thumbnail;
    result.Message = "Thumbnail generated.";
    return result;
}

bool VoxThumbnailService::RemoveForModel(
    const std::filesystem::path& modelPath,
    std::string& errorMessage)
{
    errorMessage.clear();
    const MetadataReadResult metadata = metadataService_.ReadMetadata(
        metadataService_.MetadataPathFor(modelPath));
    if (!metadata.Succeeded) return true;
    return RemoveByAssetId(metadata.Metadata.AssetId, errorMessage);
}

bool VoxThumbnailService::RemoveByAssetId(
    const std::string& assetId,
    std::string& errorMessage)
{
    const std::filesystem::path cachePath = CachePathForAssetId(assetId);
    if (cachePath.empty())
    {
        errorMessage = "Thumbnail asset ID is invalid.";
        return false;
    }
    if (!IsSafeCachePath(cachePath, errorMessage)) return false;
    std::error_code error;
    const bool exists = std::filesystem::exists(cachePath, error);
    if (error)
    {
        errorMessage = "Unable to inspect thumbnail cache: " + error.message();
        return false;
    }
    if (!exists) return true;
    const auto status = std::filesystem::symlink_status(cachePath, error);
    if (error)
    {
        errorMessage = "Unable to inspect thumbnail cache: " + error.message();
        return false;
    }
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status) ||
        !std::filesystem::remove(cachePath, error) || error)
    {
        errorMessage = "Thumbnail cache cannot be removed safely.";
        return false;
    }
    return true;
}

ThumbnailRebuildReport VoxThumbnailService::Rebuild()
{
    ThumbnailRebuildReport report;
    if (projectRoot_.empty())
    {
        report.Failed = 1U;
        report.Errors.push_back("No project is configured for thumbnails.");
        return report;
    }
    std::unordered_set<std::string> liveAssetIds;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(
             modelsDirectory_, error))
    {
        if (!entry.is_regular_file(error) || error || !IsVoxFile(entry.path()))
        {
            error.clear();
            ++report.Skipped;
            continue;
        }
        const ThumbnailGenerationResult generated = Generate(entry.path());
        if (generated.Status == ThumbnailGenerationStatus::Generated)
            ++report.Generated;
        else if (generated.Status == ThumbnailGenerationStatus::Unchanged)
            ++report.Unchanged;
        else
        {
            ++report.Failed;
            report.Errors.push_back(
                entry.path().filename().string() + ": " + generated.Message);
        }
        const MetadataReadResult metadata = metadataService_.ReadMetadata(
            metadataService_.MetadataPathFor(entry.path()));
        if (metadata.Succeeded)
            liveAssetIds.insert(metadata.Metadata.AssetId);
    }
    if (error)
    {
        ++report.Failed;
        report.Errors.push_back("Unable to enumerate Assets/Models: " +
            error.message());
    }
    RemoveOrphans(liveAssetIds, report);
    return report;
}

const std::filesystem::path& VoxThumbnailService::ProjectRoot() const noexcept
{
    return projectRoot_;
}

const std::filesystem::path& VoxThumbnailService::CacheDirectory() const noexcept
{
    return cacheDirectory_;
}

const std::string& VoxThumbnailService::LastError() const noexcept
{
    return lastError_;
}

bool VoxThumbnailService::IsRecognizedCacheFile(
    const std::filesystem::path& path) noexcept
{
    if (path.extension() != VoxThumbnailExtension) return false;
    return ModelAssetMetadataService::IsValidAssetId(path.stem().string());
}

bool VoxThumbnailService::EnsureCacheDirectory(
    std::string& errorMessage) const
{
    errorMessage.clear();
    std::error_code error;
    const auto cacheStatus =
        std::filesystem::symlink_status(cacheDirectory_, error);
    if (!error && std::filesystem::exists(cacheStatus))
    {
        if (std::filesystem::is_symlink(cacheStatus) ||
            !std::filesystem::is_directory(cacheStatus))
        {
            errorMessage = "Cache/Thumbnails must be a real directory.";
            return false;
        }
        return true;
    }
    error.clear();
    if (!std::filesystem::create_directories(cacheDirectory_, error) &&
        (error || !std::filesystem::is_directory(cacheDirectory_, error)))
    {
        errorMessage = "Unable to create Cache/Thumbnails: " + error.message();
        return false;
    }
    return true;
}

bool VoxThumbnailService::IsSafeCachePath(
    const std::filesystem::path& path,
    std::string& errorMessage) const
{
    errorMessage.clear();
    if (cacheDirectory_.empty() ||
        !IsSameDirectoryAsCanonical(
            path.lexically_normal().parent_path(), cacheDirectory_) ||
        !IsRecognizedCacheFile(path))
    {
        errorMessage = "Thumbnail destination is outside Cache/Thumbnails.";
        return false;
    }
    std::error_code error;
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(cacheDirectory_, error);
    if (error)
    {
        errorMessage = "Unable to validate thumbnail cache directory.";
        return false;
    }
    const std::filesystem::path canonicalParent =
        std::filesystem::weakly_canonical(path.parent_path(), error);
    if (error || !IsWithin(canonicalParent, canonicalRoot))
    {
        errorMessage = "Thumbnail destination escapes the project cache.";
        return false;
    }
    return true;
}

bool VoxThumbnailService::InstallImage(
    const std::filesystem::path& cachePath,
    const ThumbnailImage& image,
    std::string& errorMessage) const
{
    if (!EnsureCacheDirectory(errorMessage) ||
        !IsSafeCachePath(cachePath, errorMessage))
        return false;
    const std::filesystem::path temporary = cachePath.string() + ".tmp";
    const std::filesystem::path backup = cachePath.string() + ".bak";
    std::error_code error;
    if (std::filesystem::exists(temporary, error) || error ||
        std::filesystem::exists(backup, error) || error)
    {
        errorMessage = "Thumbnail temporary or backup file already exists.";
        return false;
    }
    if (!WriteThumbnailImage(temporary, image, errorMessage))
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    ThumbnailImage verified;
    if (!ReadThumbnailImage(temporary, verified, errorMessage) ||
        verified != image)
    {
        std::filesystem::remove(temporary, error);
        errorMessage = "Generated thumbnail failed validation.";
        return false;
    }
    const bool replacing = std::filesystem::exists(cachePath, error) && !error;
    if (replacing)
    {
        std::filesystem::rename(cachePath, backup, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            errorMessage = "Unable to back up existing thumbnail.";
            return false;
        }
    }
    std::filesystem::rename(temporary, cachePath, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        if (replacing) std::filesystem::rename(backup, cachePath, error);
        errorMessage = "Unable to install generated thumbnail.";
        return false;
    }
    if (replacing)
    {
        std::filesystem::remove(backup, error);
        if (error)
        {
            errorMessage = "Thumbnail installed but backup cleanup failed.";
            return false;
        }
    }
    return true;
}

ThumbnailMetadata VoxThumbnailService::BuildThumbnailMetadata(
    const ModelAssetMetadata& metadata,
    const ThumbnailStatus status,
    std::filesystem::path cachePath,
    std::string errorMessage) const
{
    ThumbnailMetadata result;
    result.Status = status;
    result.File = cachePath.filename().string();
    result.SourceSize = metadata.FileSize;
    result.SourceModifiedTime = metadata.SourceModifiedTime;
    result.GeneratorVersion = VoxThumbnailGeneratorVersion;
    result.Width = VoxThumbnailWidth;
    result.Height = VoxThumbnailHeight;
    if (metadata.Analysis)
    {
        result.AnalysisModelCount = metadata.Analysis->ModelCount;
        result.AnalysisSizeX = metadata.Analysis->SizeX;
        result.AnalysisSizeY = metadata.Analysis->SizeY;
        result.AnalysisSizeZ = metadata.Analysis->SizeZ;
        result.AnalysisVoxelCount = metadata.Analysis->VoxelCount;
    }
    result.Error = std::move(errorMessage);
    return result;
}

void VoxThumbnailService::RemoveOrphans(
    const std::unordered_set<std::string>& liveAssetIds,
    ThumbnailRebuildReport& report) const
{
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(
             cacheDirectory_, error))
    {
        const auto status = entry.symlink_status(error);
        if (error || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(status) ||
            !IsRecognizedCacheFile(entry.path()))
        {
            error.clear();
            continue;
        }
        if (liveAssetIds.contains(entry.path().stem().string())) continue;
        if (std::filesystem::remove(entry.path(), error) && !error)
            ++report.RemovedOrphans;
        else
        {
            ++report.Failed;
            report.Errors.push_back("Unable to remove orphan thumbnail: " +
                entry.path().filename().string());
            error.clear();
        }
    }
}

} // namespace VoxelForge::Editor
