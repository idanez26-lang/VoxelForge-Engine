#pragma once

#include "AssetDirectory.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace VoxelForge::Editor
{

class AssetBrowser final
{
public:
    [[nodiscard]] bool SetAssetsRoot(
        const std::filesystem::path& assetsRoot);
    void ClearAssetsRoot() noexcept;

    void Draw(bool* open);

    [[nodiscard]] bool Refresh();
    [[nodiscard]] bool SelectEntry(
        const std::filesystem::path& relativePath);
    [[nodiscard]] AssetOperationResult RenameSelectedEntry(
        std::string_view newName);
    [[nodiscard]] AssetDeleteAssessment CanDeleteSelectedEntry() const;
    [[nodiscard]] AssetOperationResult DeleteSelectedEntry();

    [[nodiscard]] const AssetDirectory& Directory() const noexcept;
    [[nodiscard]] const std::optional<std::filesystem::path>&
        SelectedRelativePath() const noexcept;

private:
    struct PendingEntryOperation final
    {
        std::filesystem::path AssetsRoot;
        std::filesystem::path RelativePath;
        std::string Name;
        AssetEntryType Type = AssetEntryType::File;
    };

    void DrawToolbar();
    void DrawEntries();
    void DrawSelection() const;
    void DrawStatusMessage() const;
    void DrawBackgroundContextMenu();
    void DrawNewFolderPopup();
    void DrawRenamePopup();
    void DrawDeletePopup();
    void RequestNewFolder();
    void RequestRename(const AssetEntry& entry);
    void RequestDelete(const AssetEntry& entry);
    void ResetPendingOperations() noexcept;
    [[nodiscard]] bool IsPendingOperationCurrent(
        const PendingEntryOperation& operation) const;
    void SynchronizeSelection();
    void SetError(std::string error);
    void SetStatus(std::string message);

    AssetDirectory directory_;
    std::optional<std::filesystem::path> selectedRelativePath_;
    std::optional<std::filesystem::path> newFolderAssetsRoot_;
    std::optional<PendingEntryOperation> pendingRename_;
    std::optional<PendingEntryOperation> pendingDelete_;
    std::array<char, 128> newFolderName_{};
    std::array<char, 256> renameName_{};
    std::string error_;
    std::string statusMessage_;
    bool openNewFolderPopup_ = false;
    bool openRenamePopup_ = false;
    bool openDeletePopup_ = false;
};

} // namespace VoxelForge::Editor
