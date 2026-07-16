#include "ModelImportService.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
std::string Lowercase(std::string value)
{
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}
}

bool ModelImportResult::Succeeded() const noexcept
{
    return Status == ModelImportStatus::Imported ||
        Status == ModelImportStatus::Replaced ||
        Status == ModelImportStatus::Renamed;
}

bool ModelImportResult::ChangedAssets() const noexcept
{
    return Succeeded();
}

ModelImportService::ModelImportService(
    ModelAssetMetadataService::AssetIdGenerator assetIdGenerator,
    ModelAssetMetadataService::BeforeInstallCallback beforeInstall)
    : ModelImportService(
          std::move(assetIdGenerator), std::move(beforeInstall), {})
{
}

ModelImportService::ModelImportService(
    ModelAssetMetadataService::AssetIdGenerator assetIdGenerator,
    ModelAssetMetadataService::BeforeInstallCallback beforeInstall,
    std::shared_ptr<IVoxThumbnailRenderer> thumbnailRenderer)
    : metadataService_(std::move(assetIdGenerator), std::move(beforeInstall)),
      thumbnailService_(std::move(thumbnailRenderer))
{
}

bool ModelImportService::SetProjectRoot(
    const std::filesystem::path& projectRoot)
{
    lastError_.clear();
    if (projectRoot.empty())
    {
        lastError_ = "Project root cannot be empty.";
        return false;
    }

    std::error_code error;
    const std::filesystem::path absoluteRoot =
        std::filesystem::absolute(projectRoot, error).lexically_normal();
    if (error || !std::filesystem::is_directory(absoluteRoot, error) || error)
    {
        lastError_ = "Project root does not exist or is not accessible.";
        return false;
    }

    const std::filesystem::path modelsDirectory =
        absoluteRoot / "Assets" / "Models";
    if (!metadataService_.SetModelsDirectory(modelsDirectory))
    {
        lastError_ = "Unable to configure Assets/Models metadata.";
        return false;
    }
    if (!thumbnailService_.SetProjectRoot(absoluteRoot))
    {
        metadataService_.ClearModelsDirectory();
        lastError_ = thumbnailService_.LastError();
        return false;
    }
    projectRoot_ = absoluteRoot;
    recentImports_.clear();
    return true;
}

void ModelImportService::ClearProjectRoot() noexcept
{
    projectRoot_.clear();
    recentImports_.clear();
    lastError_.clear();
    metadataService_.ClearModelsDirectory();
    thumbnailService_.ClearProject();
}

MetadataRebuildReport ModelImportService::RebuildMetadata()
{
    MetadataRebuildReport report = metadataService_.RebuildMetadata();
    NotifyRefresh();
    return report;
}

ThumbnailRebuildReport ModelImportService::RebuildThumbnails()
{
    ThumbnailRebuildReport report = thumbnailService_.Rebuild();
    NotifyRefresh();
    return report;
}

MetadataReadResult ModelImportService::ReadMetadataForModel(
    const std::filesystem::path& modelPath) const
{
    return metadataService_.ReadMetadata(metadataService_.MetadataPathFor(modelPath));
}

MetadataAnalysisResult ModelImportService::AnalyzeModel(
    const std::filesystem::path& modelPath,
    const bool forceReanalysis)
{
    return metadataService_.AnalyzeAndUpdateMetadata(
        modelPath, forceReanalysis);
}

void ModelImportService::SetRefreshCallback(RefreshCallback callback)
{
    refreshCallback_ = std::move(callback);
}

ModelImportResult ModelImportService::ImportModel(
    const std::filesystem::path& sourcePath,
    const ModelImportCollisionAction collisionAction)
{
    return ImportModelImpl(sourcePath, collisionAction, true);
}

std::vector<ModelImportResult> ModelImportService::ImportModels(
    const std::vector<std::filesystem::path>& sourcePaths,
    const ModelImportCollisionAction collisionAction)
{
    std::vector<ModelImportResult> results;
    results.reserve(sourcePaths.size());
    bool changedAssets = false;

    for (const std::filesystem::path& sourcePath : sourcePaths)
    {
        ModelImportResult result =
            ImportModelImpl(sourcePath, collisionAction, false);
        changedAssets |= result.ChangedAssets();
        const bool stop = result.Status == ModelImportStatus::Cancelled ||
            result.Status == ModelImportStatus::Collision;
        results.push_back(std::move(result));
        if (stop)
        {
            break;
        }
    }

    if (changedAssets)
    {
        NotifyRefresh();
    }
    return results;
}

const std::filesystem::path& ModelImportService::ProjectRoot() const noexcept
{
    return projectRoot_;
}

std::filesystem::path ModelImportService::ModelsDirectory() const
{
    return projectRoot_.empty()
        ? std::filesystem::path{}
        : projectRoot_ / "Assets" / "Models";
}

const std::vector<std::filesystem::path>&
ModelImportService::RecentImports() const noexcept
{
    return recentImports_;
}

const std::string& ModelImportService::LastError() const noexcept
{
    return lastError_;
}

ModelImportFormat ModelImportService::ClassifyFormat(
    const std::filesystem::path& path)
{
    const std::string extension = Lowercase(path.extension().string());
    if (extension == ".vox") return ModelImportFormat::Vox;
    if (extension == ".vfvoxel") return ModelImportFormat::VoxelForgeVoxel;
    if (extension == ".qb") return ModelImportFormat::Qubicle;
    if (extension == ".obj") return ModelImportFormat::WavefrontObj;
    return ModelImportFormat::Unsupported;
}

bool ModelImportService::IsFormatEnabled(const ModelImportFormat format) noexcept
{
    return format == ModelImportFormat::Vox;
}

ModelImportResult ModelImportService::ImportModelImpl(
    const std::filesystem::path& sourcePath,
    const ModelImportCollisionAction collisionAction,
    const bool notifyRefresh)
{
    lastError_.clear();
    if (projectRoot_.empty())
    {
        return Fail(ModelImportStatus::Failed, sourcePath, {},
            "No project is loaded.");
    }
    if (!IsFormatEnabled(ClassifyFormat(sourcePath)))
    {
        return Fail(ModelImportStatus::Unsupported, sourcePath, {},
            "Only MagicaVoxel .vox import is enabled in this version.");
    }

    std::error_code error;
    const std::filesystem::path absoluteSource =
        std::filesystem::absolute(sourcePath, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(absoluteSource, error) || error)
    {
        return Fail(ModelImportStatus::Failed, sourcePath, {},
            "Import source does not exist or is not accessible.");
    }
    const Asset::Vox::VoxModelAnalysis sourceAnalysis =
        Asset::Vox::VoxModelAnalyzer{}.Analyze(absoluteSource);
    if (!sourceAnalysis.Valid)
    {
        return Fail(ModelImportStatus::Failed, absoluteSource, {},
            "Invalid VOX import refused: " + sourceAnalysis.Error);
    }

    const std::filesystem::path modelsDirectory = ModelsDirectory();
    std::filesystem::create_directories(modelsDirectory, error);
    if (error || !std::filesystem::is_directory(modelsDirectory, error) || error)
    {
        return Fail(ModelImportStatus::Failed, absoluteSource, modelsDirectory,
            "Unable to create Assets/Models: " + error.message());
    }

    std::filesystem::path destination =
        modelsDirectory / absoluteSource.filename();
    const bool destinationExists = std::filesystem::exists(destination, error);
    if (error)
    {
        return Fail(ModelImportStatus::Failed, absoluteSource, destination,
            "Unable to inspect the import destination: " + error.message());
    }

    ModelImportStatus successStatus = ModelImportStatus::Imported;
    if (destinationExists)
    {
        switch (collisionAction)
        {
        case ModelImportCollisionAction::Ask:
            return Fail(ModelImportStatus::Collision, absoluteSource, destination,
                "The file already exists.");
        case ModelImportCollisionAction::Replace:
            successStatus = ModelImportStatus::Replaced;
            break;
        case ModelImportCollisionAction::Rename:
            destination = NextAvailablePath(destination);
            successStatus = ModelImportStatus::Renamed;
            break;
        case ModelImportCollisionAction::Skip:
            return Fail(ModelImportStatus::Skipped, absoluteSource, destination,
                "Import skipped.");
        case ModelImportCollisionAction::Cancel:
            return Fail(ModelImportStatus::Cancelled, absoluteSource, destination,
                "Import cancelled.");
        }
    }
    else
    {
        const std::filesystem::path orphanMetadata =
            metadataService_.MetadataPathFor(destination);
        if (std::filesystem::exists(orphanMetadata, error) || error)
        {
            return Fail(ModelImportStatus::Failed, absoluteSource, destination,
                "An orphan metadata file already occupies the import destination.");
        }
    }

    if (absoluteSource == destination)
    {
        return Fail(ModelImportStatus::Skipped, absoluteSource, destination,
            "Source is already in Assets/Models.");
    }

    const std::filesystem::path importTemporary =
        destination.string() + ".import.tmp";
    const std::filesystem::path importBackup =
        destination.string() + ".import.bak";
    const std::filesystem::path metadataPath =
        metadataService_.MetadataPathFor(destination);
    const std::filesystem::path metadataImportBackup =
        metadataPath.string() + ".import.bak";
    if (std::filesystem::exists(importTemporary, error) || error ||
        std::filesystem::exists(importBackup, error) || error ||
        std::filesystem::exists(metadataImportBackup, error) || error)
    {
        return Fail(ModelImportStatus::Failed, absoluteSource, destination,
            "An import temporary or backup file already exists.");
    }

    error.clear();
    const bool copied = std::filesystem::copy_file(
        absoluteSource, importTemporary,
        std::filesystem::copy_options::none, error);
    if (!copied || error)
    {
        return Fail(ModelImportStatus::Failed, absoluteSource, destination,
            "Unable to copy the model: " + error.message());
    }

    const bool replacing = successStatus == ModelImportStatus::Replaced;
    if (replacing)
    {
        const bool hasMetadata = std::filesystem::exists(metadataPath, error);
        if (error)
        {
            std::filesystem::remove(importTemporary, error);
            return Fail(ModelImportStatus::Failed, absoluteSource, destination,
                "Unable to inspect existing model metadata.");
        }
        if (hasMetadata && !std::filesystem::copy_file(
                metadataPath, metadataImportBackup,
                std::filesystem::copy_options::none, error))
        {
            std::error_code cleanup;
            std::filesystem::remove(importTemporary, cleanup);
            return Fail(ModelImportStatus::Failed, absoluteSource, destination,
                "Unable to back up existing model metadata: " +
                    error.message());
        }
        std::filesystem::rename(destination, importBackup, error);
        if (error)
        {
            std::error_code cleanup;
            std::filesystem::remove(importTemporary, cleanup);
            std::filesystem::remove(metadataImportBackup, cleanup);
            return Fail(ModelImportStatus::Failed, absoluteSource, destination,
                "Unable to back up the existing model: " + error.message());
        }
    }
    error.clear();
    std::filesystem::rename(importTemporary, destination, error);
    if (error)
    {
        std::error_code cleanup;
        std::filesystem::remove(importTemporary, cleanup);
        if (replacing)
        {
            std::filesystem::rename(importBackup, destination, cleanup);
            std::filesystem::remove(metadataImportBackup, cleanup);
        }
        return Fail(ModelImportStatus::Failed, absoluteSource, destination,
            "Unable to install the imported model: " + error.message());
    }

    const MetadataAnalysisResult metadata =
        metadataService_.AnalyzeAndUpdateMetadata(destination);
    if (!metadata.Succeeded())
    {
        std::error_code cleanup;
        std::filesystem::remove(destination, cleanup);
        std::filesystem::remove(metadataPath, cleanup);
        if (replacing)
        {
            std::filesystem::rename(importBackup, destination, cleanup);
            if (std::filesystem::exists(metadataImportBackup, cleanup))
                std::filesystem::rename(
                    metadataImportBackup, metadataPath, cleanup);
        }
        return Fail(ModelImportStatus::Failed, absoluteSource, destination,
            "Unable to analyze imported model: " + metadata.Message);
    }
    const ThumbnailGenerationResult thumbnail =
        thumbnailService_.Generate(destination, true);
    if (replacing)
    {
        std::filesystem::remove(importBackup, error);
        if (error)
            return Fail(ModelImportStatus::Failed, absoluteSource, destination,
                "Import succeeded, but model backup cleanup failed.");
        std::filesystem::remove(metadataImportBackup, error);
        if (error)
            return Fail(ModelImportStatus::Failed, absoluteSource, destination,
                "Import succeeded, but metadata backup cleanup failed.");
    }

    RecordRecentImport(destination);
    if (notifyRefresh)
    {
        NotifyRefresh();
    }
    return {
        successStatus,
        absoluteSource,
        destination,
        successStatus == ModelImportStatus::Renamed
            ? "Model imported with a new name."
            : successStatus == ModelImportStatus::Replaced
            ? "Existing model replaced."
            : "Model imported.",
        thumbnail.Status,
        thumbnail.Message};
}

std::filesystem::path ModelImportService::NextAvailablePath(
    const std::filesystem::path& destinationPath) const
{
    const std::filesystem::path parent = destinationPath.parent_path();
    const std::string stem = destinationPath.stem().string();
    const std::string extension = destinationPath.extension().string();
    std::error_code error;
    for (std::size_t index = 1U; ; ++index)
    {
        const std::filesystem::path candidate =
            parent / (stem + " (" + std::to_string(index) + ")" + extension);
        if (!std::filesystem::exists(candidate, error) && !error &&
            !std::filesystem::exists(
                metadataService_.MetadataPathFor(candidate), error) && !error)
        {
            return candidate;
        }
        error.clear();
    }
}

void ModelImportService::RecordRecentImport(
    const std::filesystem::path& destinationPath)
{
    recentImports_.erase(
        std::remove(recentImports_.begin(), recentImports_.end(), destinationPath),
        recentImports_.end());
    recentImports_.insert(recentImports_.begin(), destinationPath);
    if (recentImports_.size() > MaximumRecentImports)
    {
        recentImports_.resize(MaximumRecentImports);
    }
}

void ModelImportService::NotifyRefresh() const
{
    if (refreshCallback_)
    {
        refreshCallback_();
    }
}

ModelImportResult ModelImportService::Fail(
    const ModelImportStatus status,
    std::filesystem::path sourcePath,
    std::filesystem::path destinationPath,
    std::string message)
{
    lastError_ = message;
    return {status, std::move(sourcePath), std::move(destinationPath),
        std::move(message)};
}

} // namespace VoxelForge::Editor
