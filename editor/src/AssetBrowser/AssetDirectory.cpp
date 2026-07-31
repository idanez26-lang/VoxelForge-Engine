#include "AssetDirectory.h"

#include "ModelImport/ModelAssetMetadataService.h"
#include "Thumbnail/VoxThumbnailService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <system_error>
#include <utility>
#include "EditorPathCompare.h"

namespace VoxelForge::Editor
{

namespace
{
std::string FoldCase(const std::string_view text)
{
    std::string folded(text);
    std::transform(
        folded.begin(),
        folded.end(),
        folded.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return folded;
}

bool ContainsInvalidWindowsCharacter(const std::string_view name)
{
    static constexpr std::string_view InvalidCharacters = "<>:\"/\\|?*";

    return std::any_of(
        name.begin(),
        name.end(),
        [](const unsigned char character)
        {
            return character < 32U ||
                InvalidCharacters.find(static_cast<char>(character)) !=
                    std::string_view::npos;
        });
}

bool IsReservedWindowsName(const std::string_view name)
{
    const std::size_t extensionPosition = name.find('.');
    const std::string baseName = FoldCase(name.substr(0, extensionPosition));
    constexpr std::array<std::string_view, 4> ReservedNames = {
        "con", "prn", "aux", "nul"};

    if (std::ranges::find(ReservedNames, baseName) != ReservedNames.end())
    {
        return true;
    }

    if (baseName.size() != 4U)
    {
        return false;
    }

    const bool numberedDevice =
        baseName.starts_with("com") || baseName.starts_with("lpt");
    return numberedDevice && baseName.back() >= '1' &&
        baseName.back() <= '9';
}

bool IsValidEntryName(const std::string_view name, std::string& error)
{
    if (name.empty())
    {
        error = "Name cannot be empty.";
        return false;
    }

    if (name == "." || name == "..")
    {
        error = "Name cannot be '.' or '..'.";
        return false;
    }

    if (ContainsInvalidWindowsCharacter(name))
    {
        error = "Name contains a character forbidden on Windows.";
        return false;
    }

    if (name.back() == ' ' || name.back() == '.')
    {
        error = "Name cannot end with a space or a period.";
        return false;
    }

    if (IsReservedWindowsName(name))
    {
        error = "Name is reserved on Windows.";
        return false;
    }

    return true;
}

bool AssetEntryLess(const AssetEntry& left, const AssetEntry& right)
{
    if (left.Type() != right.Type())
    {
        return left.IsDirectory();
    }

    const std::string leftName = FoldCase(left.Name());
    const std::string rightName = FoldCase(right.Name());

    if (leftName != rightName)
    {
        return leftName < rightName;
    }

    return left.Name() < right.Name();
}

bool IsPathSameOrWithin(
    const std::filesystem::path& path,
    const std::filesystem::path& parent)
{
    const std::filesystem::path relative = path.lexically_relative(parent);

    if (relative.empty() || relative.is_absolute())
    {
        return path == parent;
    }

    return std::none_of(
        relative.begin(),
        relative.end(),
        [](const std::filesystem::path& component)
        {
            return component == "..";
        });
}

bool InspectEntryExistence(
    const std::filesystem::path& path,
    bool& entryExists,
    std::error_code& error)
{
    entryExists = std::filesystem::exists(path, error);

    if (error || entryExists)
    {
        return !error;
    }

    const std::filesystem::file_status linkStatus =
        std::filesystem::symlink_status(path, error);

    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
        return true;
    }

    if (error)
    {
        return false;
    }

    entryExists = std::filesystem::is_symlink(linkStatus);
    return true;
}
}

struct AssetDirectory::ResolvedEntryPath final
{
    std::filesystem::path OperationPath;
    std::filesystem::path CanonicalPath;
    std::filesystem::path RelativePath;
    bool IsDirectory = false;
    bool IsRegularFile = false;
    bool IsSymbolicLink = false;
};

bool AssetDirectory::SetAssetsRoot(
    const std::filesystem::path& assetsRoot)
{
    if (assetsRoot.empty())
    {
        SetError("Assets root path cannot be empty.");
        return false;
    }

    std::error_code error;
    std::filesystem::path resolvedRoot =
        std::filesystem::weakly_canonical(assetsRoot, error);

    if (error)
    {
        SetError("Unable to resolve the Assets root: " + error.message());
        return false;
    }

    if (!std::filesystem::is_directory(resolvedRoot, error) || error)
    {
        SetError("Assets root does not exist or is not accessible.");
        return false;
    }

    if (HasAssetsRoot())
    {
        const bool sameRoot =
            std::filesystem::equivalent(assetsRoot_, resolvedRoot, error);

        if (!error && sameRoot)
        {
            lastError_.clear();
            return true;
        }
    }

    assetsRoot_ = std::move(resolvedRoot);
    currentPath_ = assetsRoot_;
    entries_.clear();
    return Refresh();
}

void AssetDirectory::Clear() noexcept
{
    assetsRoot_.clear();
    currentPath_.clear();
    entries_.clear();
    lastError_.clear();
}

bool AssetDirectory::Refresh()
{
    if (!HasAssetsRoot())
    {
        SetError("No Assets root is configured.");
        return false;
    }

    std::error_code error;

    if (!std::filesystem::is_directory(currentPath_, error) || error)
    {
        error.clear();

        if (currentPath_ != assetsRoot_ &&
            std::filesystem::is_directory(assetsRoot_, error) && !error)
        {
            currentPath_ = assetsRoot_;
        }
        else
        {
            entries_.clear();
            SetError(
                "Current asset folder does not exist or is not accessible.");
            return false;
        }
    }

    std::vector<AssetEntry> refreshedEntries;
    std::filesystem::directory_iterator iterator(currentPath_, error);
    const std::filesystem::directory_iterator end;

    if (error)
    {
        SetError("Unable to read the current asset folder: " +
            error.message());
        return false;
    }

    while (iterator != end)
    {
        const std::filesystem::directory_entry& directoryEntry = *iterator;
        std::error_code entryError;
        const bool isDirectory = directoryEntry.is_directory(entryError);

        if (entryError)
        {
            SetError("Unable to inspect an asset entry: " +
                entryError.message());
            return false;
        }

        const bool isFile = !isDirectory &&
            directoryEntry.is_regular_file(entryError);

        if (entryError)
        {
            SetError("Unable to inspect an asset entry: " +
                entryError.message());
            return false;
        }

        if (isDirectory || isFile)
        {
            std::filesystem::path operationPath =
                std::filesystem::absolute(
                    directoryEntry.path(),
                    entryError).lexically_normal();

            if (entryError)
            {
                SetError("Unable to resolve an asset entry path: " +
                    entryError.message());
                return false;
            }

            const std::filesystem::path canonicalPath =
                std::filesystem::weakly_canonical(
                    operationPath,
                    entryError);

            if (entryError)
            {
                SetError("Unable to resolve an asset entry path: " +
                    entryError.message());
                return false;
            }

            if (IsWithinAssetsRoot(operationPath) &&
                IsWithinAssetsRoot(canonicalPath))
            {
                std::optional<std::uintmax_t> fileSize;

                if (isFile)
                {
                    const std::uintmax_t size =
                        directoryEntry.file_size(entryError);

                    if (!entryError)
                    {
                        fileSize = size;
                    }

                    entryError.clear();
                }

                std::optional<std::filesystem::file_time_type> lastWriteTime;
                const std::filesystem::file_time_type writeTime =
                    directoryEntry.last_write_time(entryError);

                if (!entryError)
                {
                    lastWriteTime = writeTime;
                }

                entryError.clear();
                const std::filesystem::path relativePath =
                    operationPath.lexically_relative(assetsRoot_);
                refreshedEntries.emplace_back(
                    operationPath.filename().string(),
                    std::move(operationPath),
                    relativePath,
                    isDirectory ? AssetEntryType::Directory
                                : AssetEntryType::File,
                    isFile ? directoryEntry.path().extension().string()
                           : std::string{},
                    fileSize,
                    lastWriteTime);
            }
        }

        iterator.increment(error);

        if (error)
        {
            SetError("Unable to continue reading the current asset folder: " +
                error.message());
            return false;
        }
    }

    std::sort(
        refreshedEntries.begin(),
        refreshedEntries.end(),
        AssetEntryLess);
    entries_ = std::move(refreshedEntries);
    lastError_.clear();
    return true;
}

bool AssetDirectory::EnterDirectory(
    const std::filesystem::path& directoryPath)
{
    if (!HasAssetsRoot())
    {
        SetError("No Assets root is configured.");
        return false;
    }

    const std::filesystem::path destination = directoryPath.is_absolute()
        ? directoryPath
        : currentPath_ / directoryPath;
    return ChangeDirectory(destination);
}

bool AssetDirectory::Back()
{
    if (!HasAssetsRoot())
    {
        SetError("No Assets root is configured.");
        return false;
    }

    if (!CanGoBack())
    {
        lastError_.clear();
        return true;
    }

    return ChangeDirectory(currentPath_.parent_path());
}

bool AssetDirectory::GoToAssetsRoot()
{
    if (!HasAssetsRoot())
    {
        SetError("No Assets root is configured.");
        return false;
    }

    if (currentPath_ == assetsRoot_)
    {
        lastError_.clear();
        return true;
    }

    return ChangeDirectory(assetsRoot_);
}

bool AssetDirectory::CreateFolder(const std::string_view folderName)
{
    if (!HasAssetsRoot())
    {
        SetError("No Assets root is configured.");
        return false;
    }

    std::string validationError;

    if (!IsValidEntryName(folderName, validationError))
    {
        SetError(std::move(validationError));
        return false;
    }

    const std::filesystem::path folderPath =
        currentPath_ / std::string(folderName);
    std::error_code error;
    const std::filesystem::path resolvedParent =
        std::filesystem::weakly_canonical(folderPath.parent_path(), error);
    const std::filesystem::path resolvedFolderPath =
        (resolvedParent / folderPath.filename()).lexically_normal();

    if (error || !IsStrictlyWithinAssetsRoot(resolvedFolderPath))
    {
        SetError("The new folder must stay inside the Assets root.");
        return false;
    }

    const bool alreadyExists = std::filesystem::exists(folderPath, error);

    if (error)
    {
        SetError("Unable to inspect the new folder destination: " +
            error.message());
        return false;
    }

    if (alreadyExists)
    {
        SetError("A file or folder with this name already exists.");
        return false;
    }

    const bool folderCreated =
        std::filesystem::create_directory(folderPath, error);

    if (error)
    {
        SetError("Unable to create the asset folder: " + error.message());
        return false;
    }

    if (!folderCreated)
    {
        SetError("The asset folder could not be created.");
        return false;
    }

    return Refresh();
}

AssetOperationResult AssetDirectory::RenameEntry(
    const std::filesystem::path& entryPath,
    const std::string_view newName)
{
    ResolvedEntryPath source;
    std::string operationError;

    if (!ResolveEntryForOperation(entryPath, source, operationError))
    {
        return OperationFailure(std::move(operationError), true);
    }

    std::string validationError;

    if (!IsValidEntryName(newName, validationError))
    {
        return OperationFailure(std::move(validationError), false);
    }

    std::string effectiveName(newName);
    const std::filesystem::path requestedName(effectiveName);

    if (source.IsRegularFile && requestedName.extension().empty())
    {
        effectiveName += source.OperationPath.extension().string();
    }

    if (!IsValidEntryName(effectiveName, validationError))
    {
        return OperationFailure(std::move(validationError), false);
    }

    const std::filesystem::path destination =
        (source.OperationPath.parent_path() / effectiveName)
            .lexically_normal();

    if (!IsStrictlyWithinAssetsRoot(destination))
    {
        return OperationFailure(
            "Rename destination must stay inside the Assets root.",
            false);
    }

    std::error_code error;
    const std::filesystem::path resolvedParent =
        std::filesystem::weakly_canonical(
            destination.parent_path(),
            error);

    if (error || !IsWithinAssetsRoot(resolvedParent))
    {
        return OperationFailure(
            "Unable to validate the rename destination.",
            false);
    }

    if (source.OperationPath == destination)
    {
        lastError_.clear();
        return {
            true,
            "Name is unchanged.",
            source.RelativePath};
    }

    bool destinationExists = false;

    if (!InspectEntryExistence(destination, destinationExists, error))
    {
        return OperationFailure(
            "Unable to inspect the rename destination: " +
                error.message(),
            true);
    }

    if (destinationExists)
    {
        error.clear();
        const bool sameEntry = std::filesystem::equivalent(
            source.OperationPath,
            destination,
            error);

        if (error || !sameEntry)
        {
            return OperationFailure(
                "A file or folder with this name already exists.",
                true);
        }
    }

    ResolvedEntryPath freshSource;

    if (!ResolveEntryForOperation(entryPath, freshSource, operationError))
    {
        return OperationFailure(std::move(operationError), true);
    }

    error.clear();
    destinationExists = false;

    if (!InspectEntryExistence(destination, destinationExists, error))
    {
        return OperationFailure(
            "Unable to revalidate the rename destination: " +
                error.message(),
            true);
    }

    if (destinationExists)
    {
        error.clear();
        const bool sameEntry = std::filesystem::equivalent(
            freshSource.OperationPath,
            destination,
            error);

        if (error || !sameEntry)
        {
            return OperationFailure(
                "Rename destination appeared before confirmation.",
                true);
        }
    }

    const bool currentPathMoves = freshSource.IsDirectory &&
        !freshSource.IsSymbolicLink &&
        IsPathSameOrWithin(currentPath_, freshSource.OperationPath);
    const std::filesystem::path currentSuffix = currentPathMoves
        ? currentPath_.lexically_relative(freshSource.OperationPath)
        : std::filesystem::path{};

    const bool isManagedVox = freshSource.IsRegularFile &&
        !freshSource.IsSymbolicLink &&
        FoldCase(freshSource.OperationPath.extension().string()) == ".vox" &&
        IsSameDirectoryAsCanonical(
            freshSource.OperationPath.parent_path(),
            std::filesystem::weakly_canonical(assetsRoot_ / "Models"));
    ModelAssetMetadataService metadataService;
    std::filesystem::path sourceMetadata;
    std::filesystem::path destinationMetadata;
    bool hasMetadata = false;
    if (isManagedVox)
    {
        if (!metadataService.SetModelsDirectory(assetsRoot_ / "Models"))
            return OperationFailure("Unable to configure model metadata.", true);
        sourceMetadata = metadataService.MetadataPathFor(freshSource.OperationPath);
        destinationMetadata = metadataService.MetadataPathFor(destination);
        const auto metadataStatus = std::filesystem::symlink_status(sourceMetadata, error);
        if (!error && std::filesystem::exists(metadataStatus))
        {
            if (std::filesystem::is_symlink(metadataStatus) ||
                !std::filesystem::is_regular_file(metadataStatus))
                return OperationFailure("Model metadata is not a safe regular file.", true);
            hasMetadata = true;
            if (std::filesystem::exists(destinationMetadata, error) || error)
                return OperationFailure("Rename destination metadata already exists.", true);
        }
        else if (error)
        {
            return OperationFailure("Unable to inspect model metadata.", true);
        }
    }

    error.clear();
    std::filesystem::rename(
        freshSource.OperationPath,
        destination,
        error);

    if (error)
    {
        return OperationFailure(
            "Unable to rename the asset entry: " + error.message(),
            true);
    }

    if (isManagedVox)
    {
        if (hasMetadata)
        {
            std::filesystem::rename(sourceMetadata, destinationMetadata, error);
            if (error)
            {
                std::error_code rollback;
                std::filesystem::rename(
                    destination, freshSource.OperationPath, rollback);
                return OperationFailure(
                    "Unable to rename model metadata: " + error.message(), true);
            }
        }
        const MetadataOperationResult ensured =
            metadataService.EnsureMetadata(destination);
        if (!ensured.Succeeded())
        {
            std::error_code rollback;
            if (hasMetadata)
                std::filesystem::rename(
                    destinationMetadata, sourceMetadata, rollback);
            else
                std::filesystem::remove(destinationMetadata, rollback);
            std::filesystem::rename(destination, freshSource.OperationPath, rollback);
            return OperationFailure(
                "Unable to update renamed model metadata: " + ensured.Message,
                true);
        }
    }

    if (currentPathMoves)
    {
        error.clear();
        const std::filesystem::path movedCurrentPath =
            currentSuffix == "."
            ? destination
            : destination / currentSuffix;
        currentPath_ = std::filesystem::weakly_canonical(
            movedCurrentPath,
            error);

        if (error)
        {
            currentPath_ = assetsRoot_;
        }
    }

    const std::filesystem::path resultingRelativePath =
        destination.lexically_relative(assetsRoot_);
    const bool refreshed = Refresh();
    std::string message = "Renamed to " + effectiveName + ".";

    if (!refreshed)
    {
        message += " Refresh failed: " + lastError_;
    }

    return {true, std::move(message), resultingRelativePath};
}

AssetDeleteAssessment AssetDirectory::CanDeleteEntry(
    const std::filesystem::path& entryPath) const
{
    ResolvedEntryPath entry;
    std::string operationError;

    if (!ResolveEntryForOperation(entryPath, entry, operationError))
    {
        return {
            false,
            false,
            false,
            false,
            {},
            std::move(operationError)};
    }

    AssetDeleteAssessment assessment;
    assessment.IsDirectory = entry.IsDirectory;
    assessment.IsSymbolicLink = entry.IsSymbolicLink;
    assessment.RelativePath = entry.RelativePath;

    if (entry.IsDirectory && !entry.IsSymbolicLink)
    {
        std::error_code error;
        std::filesystem::directory_iterator iterator(
            entry.OperationPath,
            error);

        if (error)
        {
            assessment.Message =
                "Unable to inspect the folder before deletion: " +
                error.message();
            return assessment;
        }

        if (iterator != std::filesystem::directory_iterator{})
        {
            assessment.IsNonEmptyDirectory = true;
            assessment.Message = "Folder is not empty.";
            return assessment;
        }
    }

    assessment.CanDelete = true;
    assessment.Message = "Deletion is allowed after confirmation.";
    return assessment;
}

AssetOperationResult AssetDirectory::DeleteEntry(
    const std::filesystem::path& entryPath)
{
    const AssetDeleteAssessment assessment = CanDeleteEntry(entryPath);

    if (!assessment.CanDelete)
    {
        return OperationFailure(assessment.Message, true);
    }

    ResolvedEntryPath freshEntry;
    std::string operationError;

    if (!ResolveEntryForOperation(entryPath, freshEntry, operationError))
    {
        return OperationFailure(std::move(operationError), true);
    }

    std::error_code error;
    const bool isManagedVox = freshEntry.IsRegularFile &&
        !freshEntry.IsSymbolicLink &&
        FoldCase(freshEntry.OperationPath.extension().string()) == ".vox" &&
        IsSameDirectoryAsCanonical(
            freshEntry.OperationPath.parent_path(),
            std::filesystem::weakly_canonical(assetsRoot_ / "Models"));
    std::filesystem::path metadataPath;
    std::filesystem::path metadataTemporary;
    std::string thumbnailAssetId;
    bool hasMetadata = false;
    if (isManagedVox)
    {
        ModelAssetMetadataService metadataService;
        if (!metadataService.SetModelsDirectory(assetsRoot_ / "Models"))
            return OperationFailure("Unable to configure model metadata.", true);
        metadataPath = metadataService.MetadataPathFor(freshEntry.OperationPath);
        metadataTemporary = metadataPath.string() + ".delete.tmp";
        const auto metadataStatus = std::filesystem::symlink_status(metadataPath, error);
        if (!error && std::filesystem::exists(metadataStatus))
        {
            if (std::filesystem::is_symlink(metadataStatus) ||
                !std::filesystem::is_regular_file(metadataStatus) ||
                std::filesystem::exists(metadataTemporary, error) || error)
                return OperationFailure("Model metadata cannot be deleted safely.", true);
            const MetadataReadResult metadata =
                metadataService.ReadMetadata(metadataPath);
            if (metadata.Succeeded)
                thumbnailAssetId = metadata.Metadata.AssetId;
            std::filesystem::rename(metadataPath, metadataTemporary, error);
            if (error)
                return OperationFailure("Unable to prepare model metadata deletion.", true);
            hasMetadata = true;
        }
        else if (error)
            return OperationFailure("Unable to inspect model metadata.", true);
    }

    error.clear();
    const bool removed = std::filesystem::remove(
        freshEntry.OperationPath,
        error);

    if (error)
    {
        if (hasMetadata)
        {
            std::error_code rollback;
            std::filesystem::rename(metadataTemporary, metadataPath, rollback);
        }
        return OperationFailure(
            "Unable to delete the asset entry: " + error.message(),
            true);
    }

    if (!removed)
    {
        if (hasMetadata)
        {
            std::error_code rollback;
            std::filesystem::rename(metadataTemporary, metadataPath, rollback);
        }
        return OperationFailure(
            freshEntry.IsDirectory
                ? "Folder is not empty."
                : "Asset entry no longer exists.",
            true);
    }

    if (hasMetadata)
    {
        std::filesystem::remove(metadataTemporary, error);
        if (error)
            return OperationFailure(
                "Model was deleted, but metadata cleanup failed: " +
                    error.message(), true);
    }

    std::string thumbnailCleanupError;
    if (!thumbnailAssetId.empty())
    {
        VoxThumbnailService thumbnailService;
        if (!thumbnailService.SetProjectRoot(assetsRoot_.parent_path()))
            thumbnailCleanupError = thumbnailService.LastError();
        else
            static_cast<void>(thumbnailService.RemoveByAssetId(
                thumbnailAssetId, thumbnailCleanupError));
    }

    const bool refreshed = Refresh();
    std::string message = "Deleted " +
        std::string(freshEntry.IsDirectory ? "folder " : "file ") +
        freshEntry.OperationPath.filename().string() + ".";

    if (!refreshed)
    {
        message += " Refresh failed: " + lastError_;
    }
    if (!thumbnailCleanupError.empty())
        message += " Thumbnail cleanup warning: " + thumbnailCleanupError;

    return {true, std::move(message), std::nullopt};
}

bool AssetDirectory::HasAssetsRoot() const noexcept
{
    return !assetsRoot_.empty();
}

bool AssetDirectory::CanGoBack() const noexcept
{
    return HasAssetsRoot() && currentPath_ != assetsRoot_;
}

bool AssetDirectory::UsesAssetsRoot(
    const std::filesystem::path& assetsRoot) const
{
    if (!HasAssetsRoot() || assetsRoot.empty())
    {
        return false;
    }

    std::error_code error;
    const bool equivalent =
        std::filesystem::equivalent(assetsRoot_, assetsRoot, error);
    return !error && equivalent;
}

const std::filesystem::path& AssetDirectory::AssetsRoot() const noexcept
{
    return assetsRoot_;
}

const std::filesystem::path& AssetDirectory::CurrentPath() const noexcept
{
    return currentPath_;
}

std::filesystem::path AssetDirectory::CurrentRelativePath() const
{
    if (!HasAssetsRoot())
    {
        return {};
    }

    const std::filesystem::path relative =
        currentPath_.lexically_relative(assetsRoot_);
    return relative == "." ? std::filesystem::path{} : relative;
}

const std::vector<AssetEntry>& AssetDirectory::Entries() const noexcept
{
    return entries_;
}

const std::string& AssetDirectory::LastError() const noexcept
{
    return lastError_;
}

bool AssetDirectory::ChangeDirectory(
    const std::filesystem::path& directoryPath)
{
    std::error_code error;
    const std::filesystem::path resolvedDirectory =
        std::filesystem::weakly_canonical(directoryPath, error);

    if (error ||
        !std::filesystem::is_directory(resolvedDirectory, error) || error)
    {
        SetError("Asset folder does not exist or is not accessible.");
        return false;
    }

    if (!IsWithinAssetsRoot(resolvedDirectory))
    {
        SetError("Asset navigation cannot leave the Assets root.");
        return false;
    }

    const std::filesystem::path previousPath = currentPath_;
    const std::vector<AssetEntry> previousEntries = entries_;
    currentPath_ = resolvedDirectory;

    if (Refresh())
    {
        return true;
    }

    const std::string refreshError = lastError_;
    currentPath_ = previousPath;
    entries_ = previousEntries;
    lastError_ = refreshError;
    return false;
}

bool AssetDirectory::ResolveEntryForOperation(
    const std::filesystem::path& entryPath,
    ResolvedEntryPath& resolvedEntry,
    std::string& errorMessage) const
{
    if (!HasAssetsRoot())
    {
        errorMessage = "No Assets root is configured.";
        return false;
    }

    if (entryPath.empty())
    {
        errorMessage = "Asset entry path cannot be empty.";
        return false;
    }

    const std::filesystem::path candidate = entryPath.is_absolute()
        ? entryPath
        : assetsRoot_ / entryPath;
    std::error_code error;
    const std::filesystem::path operationPath =
        std::filesystem::absolute(candidate, error).lexically_normal();

    if (error || !IsStrictlyWithinAssetsRoot(operationPath))
    {
        errorMessage =
            "Asset operation cannot leave the Assets root or target its root.";
        return false;
    }

    const std::filesystem::file_status linkStatus =
        std::filesystem::symlink_status(operationPath, error);

    if (error || !std::filesystem::exists(linkStatus))
    {
        errorMessage = "Asset entry no longer exists or is not accessible.";
        return false;
    }

    const std::filesystem::path canonicalPath =
        std::filesystem::weakly_canonical(operationPath, error);

    if (error || !IsStrictlyWithinAssetsRoot(canonicalPath))
    {
        errorMessage =
            "Asset operation cannot follow a path outside the Assets root.";
        return false;
    }

    const bool isSymbolicLink = std::filesystem::is_symlink(linkStatus);
    const bool isDirectory =
        std::filesystem::is_directory(operationPath, error);

    if (error)
    {
        errorMessage = "Unable to inspect the asset entry: " +
            error.message();
        return false;
    }

    const bool isRegularFile = !isDirectory &&
        std::filesystem::is_regular_file(operationPath, error);

    if (error)
    {
        errorMessage = "Unable to inspect the asset entry: " +
            error.message();
        return false;
    }

    if (!isDirectory && !isRegularFile && !isSymbolicLink)
    {
        errorMessage = "Unsupported asset entry type.";
        return false;
    }

    resolvedEntry.OperationPath = operationPath;
    resolvedEntry.CanonicalPath = canonicalPath;
    resolvedEntry.RelativePath =
        operationPath.lexically_relative(assetsRoot_);
    resolvedEntry.IsDirectory = isDirectory;
    resolvedEntry.IsRegularFile = isRegularFile;
    resolvedEntry.IsSymbolicLink = isSymbolicLink;
    return true;
}

bool AssetDirectory::IsWithinAssetsRoot(
    const std::filesystem::path& path) const
{
    return HasAssetsRoot() && IsPathSameOrWithin(path, assetsRoot_);
}

bool AssetDirectory::IsStrictlyWithinAssetsRoot(
    const std::filesystem::path& path) const
{
    return path != assetsRoot_ && IsWithinAssetsRoot(path);
}

AssetOperationResult AssetDirectory::OperationFailure(
    std::string message,
    const bool refreshEntries)
{
    if (refreshEntries && HasAssetsRoot())
    {
        static_cast<void>(Refresh());
    }

    SetError(message);
    return {false, std::move(message), std::nullopt};
}

void AssetDirectory::SetError(std::string error)
{
    lastError_ = std::move(error);
}

} // namespace VoxelForge::Editor
