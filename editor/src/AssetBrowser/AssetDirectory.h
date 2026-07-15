#pragma once

#include "AssetEntry.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor
{

class AssetDirectory final
{
public:
    [[nodiscard]] bool SetAssetsRoot(
        const std::filesystem::path& assetsRoot);
    void Clear() noexcept;

    [[nodiscard]] bool Refresh();
    [[nodiscard]] bool EnterDirectory(
        const std::filesystem::path& directoryPath);
    [[nodiscard]] bool Back();
    [[nodiscard]] bool GoToAssetsRoot();
    [[nodiscard]] bool CreateFolder(std::string_view folderName);

    [[nodiscard]] bool HasAssetsRoot() const noexcept;
    [[nodiscard]] bool CanGoBack() const noexcept;
    [[nodiscard]] bool UsesAssetsRoot(
        const std::filesystem::path& assetsRoot) const;

    [[nodiscard]] const std::filesystem::path& AssetsRoot() const noexcept;
    [[nodiscard]] const std::filesystem::path& CurrentPath() const noexcept;
    [[nodiscard]] std::filesystem::path CurrentRelativePath() const;
    [[nodiscard]] const std::vector<AssetEntry>& Entries() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

private:
    [[nodiscard]] bool ChangeDirectory(
        const std::filesystem::path& directoryPath);
    [[nodiscard]] bool IsWithinAssetsRoot(
        const std::filesystem::path& path) const;
    void SetError(std::string error);

    std::filesystem::path assetsRoot_;
    std::filesystem::path currentPath_;
    std::vector<AssetEntry> entries_;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
