#pragma once

#include "AssetEntry.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor
{

struct AssetOperationResult final
{
    bool Succeeded = false;
    std::string Message;
    std::optional<std::filesystem::path> ResultingRelativePath;
};

struct AssetDeleteAssessment final
{
    bool CanDelete = false;
    bool IsDirectory = false;
    bool IsSymbolicLink = false;
    bool IsNonEmptyDirectory = false;
    std::filesystem::path RelativePath;
    std::string Message;
};

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
    [[nodiscard]] AssetOperationResult RenameEntry(
        const std::filesystem::path& entryPath,
        std::string_view newName);
    [[nodiscard]] AssetDeleteAssessment CanDeleteEntry(
        const std::filesystem::path& entryPath) const;
    [[nodiscard]] AssetOperationResult DeleteEntry(
        const std::filesystem::path& entryPath);

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
    struct ResolvedEntryPath;

    [[nodiscard]] bool ChangeDirectory(
        const std::filesystem::path& directoryPath);
    [[nodiscard]] bool ResolveEntryForOperation(
        const std::filesystem::path& entryPath,
        ResolvedEntryPath& resolvedEntry,
        std::string& error) const;
    [[nodiscard]] bool IsWithinAssetsRoot(
        const std::filesystem::path& path) const;
    [[nodiscard]] bool IsStrictlyWithinAssetsRoot(
        const std::filesystem::path& path) const;
    [[nodiscard]] AssetOperationResult OperationFailure(
        std::string message,
        bool refreshEntries);
    void SetError(std::string error);

    std::filesystem::path assetsRoot_;
    std::filesystem::path currentPath_;
    std::vector<AssetEntry> entries_;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
