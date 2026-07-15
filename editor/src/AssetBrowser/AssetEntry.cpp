#include "AssetEntry.h"

#include <utility>

namespace VoxelForge::Editor
{

AssetEntry::AssetEntry(
    std::string name,
    std::filesystem::path absolutePath,
    std::filesystem::path relativePath,
    const AssetEntryType type,
    std::string extension,
    std::optional<std::uintmax_t> fileSize,
    std::optional<std::filesystem::file_time_type> lastWriteTime)
    : name_(std::move(name)),
      absolutePath_(std::move(absolutePath)),
      relativePath_(std::move(relativePath)),
      type_(type),
      extension_(std::move(extension)),
      fileSize_(fileSize),
      lastWriteTime_(lastWriteTime)
{
}

const std::string& AssetEntry::Name() const noexcept
{
    return name_;
}

const std::filesystem::path& AssetEntry::AbsolutePath() const noexcept
{
    return absolutePath_;
}

const std::filesystem::path& AssetEntry::RelativePath() const noexcept
{
    return relativePath_;
}

AssetEntryType AssetEntry::Type() const noexcept
{
    return type_;
}

const std::string& AssetEntry::Extension() const noexcept
{
    return extension_;
}

const std::optional<std::uintmax_t>& AssetEntry::FileSize() const noexcept
{
    return fileSize_;
}

const std::optional<std::filesystem::file_time_type>&
AssetEntry::LastWriteTime() const noexcept
{
    return lastWriteTime_;
}

bool AssetEntry::IsFile() const noexcept
{
    return type_ == AssetEntryType::File;
}

bool AssetEntry::IsDirectory() const noexcept
{
    return type_ == AssetEntryType::Directory;
}

} // namespace VoxelForge::Editor
