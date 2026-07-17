#include "VoxelModelCreationService.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
class BusyGuard final
{
public:
    explicit BusyGuard(bool& busy) noexcept : busy_(busy) { busy_ = true; }
    ~BusyGuard() { busy_ = false; }
private:
    bool& busy_;
};

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool WriteAndFlush(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string& error)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Unable to create the model temporary file.";
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output)
    {
        error = "Unable to write and flush the model temporary file.";
        output.close();
        return false;
    }
    output.close();
    if (!output)
    {
        error = "Unable to close the model temporary file safely.";
        return false;
    }
    return true;
}

bool IsSafeRegularFile(
    const std::filesystem::path& path,
    std::string& error)
{
    std::error_code filesystemError;
    const auto status = std::filesystem::symlink_status(path, filesystemError);
    if (filesystemError || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status))
    {
        error = "Model collision target is not a safe regular file.";
        return false;
    }
    return true;
}

bool RemoveRecognizedFile(
    const std::filesystem::path& path,
    std::string& error)
{
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError))
        return !filesystemError;
    if (!IsSafeRegularFile(path, error)) return false;
    if (std::filesystem::remove(path, filesystemError) && !filesystemError)
        return true;
    error = "Unable to remove recognized creation transaction file.";
    return false;
}
}

VoxelModelCreationService::VoxelModelCreationService(
    ModelAssetMetadataService::AssetIdGenerator assetIdGenerator)
    : metadataService_(std::move(assetIdGenerator))
{
}

bool VoxelModelCreationService::SetProjectRoot(
    const std::filesystem::path& projectRoot)
{
    ClearProject();
    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
    {
        lastError_ = "Project root is unavailable.";
        return false;
    }
    const std::filesystem::path models = canonical / "Assets" / "Models";
    const auto status = std::filesystem::symlink_status(models, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status))
    {
        lastError_ = "Assets/Models must be a real directory.";
        return false;
    }
    projectRoot_ = canonical;
    modelsDirectory_ = std::filesystem::weakly_canonical(models, error);
    if (error || !metadataService_.SetModelsDirectory(modelsDirectory_))
    {
        lastError_ = "Unable to configure model metadata.";
        ClearProject();
        return false;
    }
    return true;
}

void VoxelModelCreationService::ClearProject() noexcept
{
    projectRoot_.clear();
    modelsDirectory_.clear();
    lastError_.clear();
    metadataService_.ClearModelsDirectory();
    busy_ = false;
}

void VoxelModelCreationService::SetThumbnailCallback(
    ThumbnailCallback callback)
{
    thumbnailCallback_ = std::move(callback);
}

void VoxelModelCreationService::SetMetadataCallback(
    MetadataCallback callback)
{
    metadataCallback_ = std::move(callback);
}

void VoxelModelCreationService::SetAssetBrowserCallback(
    AssetBrowserCallback callback)
{
    assetBrowserCallback_ = std::move(callback);
}

void VoxelModelCreationService::SetOpenCallback(OpenCallback callback)
{
    openCallback_ = std::move(callback);
}

VoxelModelCreationResult VoxelModelCreationService::CreateModel(
    const VoxelModelCreationRequest& request,
    const VoxelModelCreationCollisionAction collisionAction)
{
    if (busy_) return Fail({}, "A model creation transaction is already active.");
    BusyGuard guard(busy_);
    std::string error;
    if (modelsDirectory_.empty())
        return Fail({}, "No project is configured for model creation.");
    if (!ValidateModelName(request.Name, error) ||
        !ValidateDimensions(request.Dimensions, error))
        return Fail({}, std::move(error));

    const std::filesystem::path desired =
        modelsDirectory_ / (request.Name + ".vox");
    std::error_code filesystemError;
    const bool collision = std::filesystem::exists(desired, filesystemError);
    if (filesystemError)
        return Fail(desired, "Unable to inspect the model destination.");
    if (collision && collisionAction == VoxelModelCreationCollisionAction::Ask)
        return {VoxelModelCreationStatus::Collision, desired, {},
            "A model with this name already exists."};
    if (collision && collisionAction == VoxelModelCreationCollisionAction::Cancel)
        return {VoxelModelCreationStatus::Cancelled, desired, {},
            "Model creation cancelled."};

    std::filesystem::path destination = desired;
    VoxelModelCreationStatus successStatus = VoxelModelCreationStatus::Created;
    if (collisionAction == VoxelModelCreationCollisionAction::Rename && collision)
    {
        destination = NextAvailablePath(desired);
        if (destination.empty())
            return Fail(desired, "Unable to find an available model name.");
        successStatus = VoxelModelCreationStatus::Renamed;
    }
    else if (collision && collisionAction == VoxelModelCreationCollisionAction::Replace)
    {
        if (!IsSafeRegularFile(desired, error))
            return Fail(desired, std::move(error));
        successStatus = VoxelModelCreationStatus::Replaced;
    }

    Asset::Vox::VoxModel source;
    source.Version = Asset::Voxel::VoxDocumentWriter::DefaultVoxVersion;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({
        {request.Dimensions.X, request.Dimensions.Y, request.Dimensions.Z},
        {}});
    source.DeclaredModelCount = 1U;
    auto built = Asset::Voxel::VoxDocumentLoader{}.Build(source, destination);
    if (!built.Succeeded()) return Fail(destination, built.Message);
    const auto serialized =
        Asset::Voxel::VoxDocumentWriter{}.Serialize(*built.Document);
    if (!serialized.Succeeded()) return Fail(destination, serialized.Message);

    const std::filesystem::path temporary = destination.string() + ".vfcreate.tmp";
    const std::filesystem::path backup = destination.string() + ".vfcreate.bak";
    if (std::filesystem::exists(temporary, filesystemError) || filesystemError ||
        std::filesystem::exists(backup, filesystemError) || filesystemError)
        return Fail(destination,
            "A recognized model creation transaction file already exists.");
    if (!WriteAndFlush(temporary, serialized.Bytes, error))
    {
        std::string cleanup;
        static_cast<void>(RemoveRecognizedFile(temporary, cleanup));
        return Fail(destination, std::move(error));
    }
    const auto verified = Asset::Voxel::VoxDocumentLoader{}.Load(temporary);
    std::string difference;
    if (!verified.Succeeded() || !Asset::Voxel::AreVoxelDocumentsEquivalent(
            *built.Document, *verified.Document, difference))
    {
        std::string cleanup;
        static_cast<void>(RemoveRecognizedFile(temporary, cleanup));
        return Fail(destination, verified.Succeeded()
            ? "Created VOX failed equivalence validation: " + difference
            : "Created VOX failed reload validation: " + verified.Message);
    }

    const bool replacing = successStatus == VoxelModelCreationStatus::Replaced;
    if (replacing)
    {
        std::filesystem::rename(destination, backup, filesystemError);
        if (filesystemError)
        {
            std::string cleanup;
            static_cast<void>(RemoveRecognizedFile(temporary, cleanup));
            return Fail(destination, "Unable to back up the existing model.");
        }
    }
    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError)
    {
        if (replacing)
        {
            std::error_code rollback;
            std::filesystem::rename(backup, destination, rollback);
        }
        std::string cleanup;
        static_cast<void>(RemoveRecognizedFile(temporary, cleanup));
        return Fail(destination, "Unable to install the new model.");
    }

    const MetadataAnalysisResult metadata = metadataCallback_
        ? metadataCallback_(destination)
        : metadataService_.AnalyzeAndUpdateMetadata(destination, true);

    VoxelModelCreationStepResult thumbnail{false,
        "Thumbnail service is unavailable."};
    if (thumbnailCallback_) thumbnail = thumbnailCallback_(destination);
    ModelAssetMetadata finalMetadata = metadata.Succeeded()
        ? metadata.Metadata : ModelAssetMetadata{};
    bool metadataAvailable = metadata.Succeeded();
    if (thumbnail.Succeeded || !metadataAvailable)
    {
        const MetadataReadResult refreshedMetadata =
            metadataService_.ReadMetadata(
                metadataService_.MetadataPathFor(destination));
        if (refreshedMetadata.Succeeded)
        {
            finalMetadata = refreshedMetadata.Metadata;
            metadataAvailable = true;
        }
    }
    const bool browserRefreshed = assetBrowserCallback_ &&
        assetBrowserCallback_(destination);
    const bool opened = openCallback_ && openCallback_(destination);

    if (replacing)
    {
        std::string cleanup;
        if (!RemoveRecognizedFile(backup, cleanup))
            return Fail(destination,
                "Model created, but replacement backup cleanup failed.");
    }

    std::string warning;
    if (!metadataAvailable)
        warning = "Metadata creation failed: " + metadata.Message;
    if (!thumbnail.Succeeded)
    {
        if (!warning.empty()) warning += " ";
        warning += "Thumbnail generation failed: " + thumbnail.Message;
    }
    if (!browserRefreshed)
    {
        if (!warning.empty()) warning += " ";
        warning += "Asset Browser refresh failed.";
    }
    if (!opened)
    {
        if (!warning.empty()) warning += " ";
        warning += "Automatic viewport opening failed.";
    }
    lastError_.clear();
    return {successStatus, destination, std::move(finalMetadata),
        successStatus == VoxelModelCreationStatus::Renamed
            ? "Voxel model created with an available name."
            : successStatus == VoxelModelCreationStatus::Replaced
            ? "Existing voxel model replaced."
            : "Voxel model created.",
        std::move(warning), thumbnail.Succeeded, browserRefreshed, opened};
}

const std::filesystem::path& VoxelModelCreationService::ProjectRoot()
    const noexcept { return projectRoot_; }
const std::filesystem::path& VoxelModelCreationService::ModelsDirectory()
    const noexcept { return modelsDirectory_; }
const std::string& VoxelModelCreationService::LastError() const noexcept
{
    return lastError_;
}

bool VoxelModelCreationService::ValidateModelName(
    const std::string& name,
    std::string& error)
{
    error.clear();
    if (name.empty())
    {
        error = "Model name cannot be empty.";
        return false;
    }
    if (name.size() > 120U || name.front() == ' ' || name.back() == ' ' ||
        name.back() == '.')
    {
        error = "Model name has an invalid length or trailing character.";
        return false;
    }
    constexpr std::string_view forbidden = "<>:\"/\\|?*";
    if (std::any_of(name.begin(), name.end(),
        [forbidden](const unsigned char character)
        {
            return character < 32U ||
                forbidden.find(static_cast<char>(character)) !=
                    std::string_view::npos;
        }))
    {
        error = "Model name contains a Windows-forbidden character.";
        return false;
    }
    static const std::unordered_set<std::string> reserved{
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
        "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2",
        "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::size_t firstDot = name.find('.');
    if (reserved.contains(LowerAscii(name.substr(0U, firstDot))))
    {
        error = "Model name is reserved by Windows.";
        return false;
    }
    return true;
}

bool VoxelModelCreationService::ValidateDimensions(
    const Asset::Voxel::VoxelDimensions& dimensions,
    std::string& error)
{
    if (dimensions.X == 0U || dimensions.Y == 0U || dimensions.Z == 0U ||
        dimensions.X > 256U || dimensions.Y > 256U || dimensions.Z > 256U)
    {
        error = "Model dimensions must be between 1 and 256 per axis.";
        return false;
    }
    return true;
}

std::filesystem::path VoxelModelCreationService::NextAvailablePath(
    const std::filesystem::path& desired) const
{
    for (std::uint32_t suffix = 1U; suffix < 100000U; ++suffix)
    {
        const std::filesystem::path candidate = desired.parent_path() /
            (desired.stem().string() + " (" + std::to_string(suffix) + ")" +
             desired.extension().string());
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error)
            return candidate;
    }
    return {};
}

VoxelModelCreationResult VoxelModelCreationService::Fail(
    std::filesystem::path path,
    std::string message)
{
    lastError_ = message;
    return {VoxelModelCreationStatus::Failed, std::move(path), {},
        std::move(message)};
}

const char* VoxelModelCreationStatusName(
    const VoxelModelCreationStatus status) noexcept
{
    switch (status)
    {
    case VoxelModelCreationStatus::Created: return "Created";
    case VoxelModelCreationStatus::Renamed: return "Renamed";
    case VoxelModelCreationStatus::Replaced: return "Replaced";
    case VoxelModelCreationStatus::Collision: return "Collision";
    case VoxelModelCreationStatus::Cancelled: return "Cancelled";
    case VoxelModelCreationStatus::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
