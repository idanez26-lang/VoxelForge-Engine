#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

enum class AssetEntryType
{
    File,
    Directory
};

class AssetEntry final
{
public:
    AssetEntry(
        std::string name,
        std::filesystem::path absolutePath,
        std::filesystem::path relativePath,
        AssetEntryType type,
        std::string extension,
        std::optional<std::uintmax_t> fileSize,
        std::optional<std::filesystem::file_time_type> lastWriteTime);

    [[nodiscard]] const std::string& Name() const noexcept;
    [[nodiscard]] const std::filesystem::path& AbsolutePath() const noexcept;
    [[nodiscard]] const std::filesystem::path& RelativePath() const noexcept;
    [[nodiscard]] AssetEntryType Type() const noexcept;
    [[nodiscard]] const std::string& Extension() const noexcept;
    [[nodiscard]] const std::optional<std::uintmax_t>& FileSize() const noexcept;
    [[nodiscard]] const std::optional<std::filesystem::file_time_type>&
        LastWriteTime() const noexcept;

    [[nodiscard]] bool IsFile() const noexcept;
    [[nodiscard]] bool IsDirectory() const noexcept;

private:
    std::string name_;
    std::filesystem::path absolutePath_;
    std::filesystem::path relativePath_;
    AssetEntryType type_ = AssetEntryType::File;
    std::string extension_;
    std::optional<std::uintmax_t> fileSize_;
    std::optional<std::filesystem::file_time_type> lastWriteTime_;
};

} // namespace VoxelForge::Editor
