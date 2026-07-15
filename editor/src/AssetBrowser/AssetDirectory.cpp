#include "AssetDirectory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <system_error>
#include <utility>

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
    constexpr std::string_view InvalidCharacters = "<>:\"/\\|?*";

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

bool IsValidFolderName(const std::string_view name, std::string& error)
{
    if (name.empty())
    {
        error = "Folder name cannot be empty.";
        return false;
    }

    if (name == "." || name == "..")
    {
        error = "Folder name cannot be . or ...";
        return false;
    }

    if (ContainsInvalidWindowsCharacter(name))
    {
        error = "Folder name contains a character forbidden on Windows.";
        return false;
    }

    if (name.back() == ' ' || name.back() == '.')
    {
        error = "Folder name cannot end with a space or a period.";
        return false;
    }

    if (IsReservedWindowsName(name))
    {
        error = "Folder name is reserved on Windows.";
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
}

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
            std::filesystem::path absolutePath =
                std::filesystem::weakly_canonical(
                    directoryEntry.path(),
                    entryError);

            if (entryError)
            {
                SetError("Unable to resolve an asset entry path: " +
                    entryError.message());
                return false;
            }

            if (IsWithinAssetsRoot(absolutePath))
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
                    absolutePath.lexically_relative(assetsRoot_);
                refreshedEntries.emplace_back(
                    absolutePath.filename().string(),
                    std::move(absolutePath),
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

    if (!IsValidFolderName(folderName, validationError))
    {
        SetError(std::move(validationError));
        return false;
    }

    const std::filesystem::path folderPath =
        currentPath_ / std::string(folderName);
    std::error_code error;
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

bool AssetDirectory::IsWithinAssetsRoot(
    const std::filesystem::path& path) const
{
    if (!HasAssetsRoot())
    {
        return false;
    }

    const std::filesystem::path relative =
        path.lexically_relative(assetsRoot_);

    if (relative.empty() || relative.is_absolute())
    {
        return path == assetsRoot_;
    }

    return std::none_of(
        relative.begin(),
        relative.end(),
        [](const std::filesystem::path& component)
        {
            return component == "..";
        });
}

void AssetDirectory::SetError(std::string error)
{
    lastError_ = std::move(error);
}

} // namespace VoxelForge::Editor
