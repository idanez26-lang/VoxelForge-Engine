#pragma once

#include "AssetDirectory.h"
#include "AssetBrowserViewModel.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor
{

class AssetBrowser final
{
public:
    using MessageCallback = std::function<void(std::string)>;
    using OpenVoxCallback = std::function<void(std::filesystem::path)>;

    [[nodiscard]] bool SetAssetsRoot(
        const std::filesystem::path& assetsRoot);
    void ClearAssetsRoot() noexcept;

    void Draw(bool* open);

    [[nodiscard]] bool Refresh();
    [[nodiscard]] std::size_t RefreshCount() const noexcept;
    [[nodiscard]] bool SelectEntry(
        const std::filesystem::path& relativePath);
    [[nodiscard]] bool RevealEntry(
        const std::filesystem::path& relativePath);
    [[nodiscard]] AssetOperationResult RenameSelectedEntry(
        std::string_view newName);
    [[nodiscard]] AssetDeleteAssessment CanDeleteSelectedEntry() const;
    [[nodiscard]] AssetOperationResult DeleteSelectedEntry();

    [[nodiscard]] const AssetDirectory& Directory() const noexcept;
    [[nodiscard]] const std::optional<std::filesystem::path>&
        SelectedRelativePath() const noexcept;
    [[nodiscard]] std::optional<AssetEntry> SelectedEntry() const;
    [[nodiscard]] AssetBrowserViewSettings& ViewSettings() noexcept;
    [[nodiscard]] const AssetBrowserViewSettings& ViewSettings() const noexcept;
    void SetSearchText(std::string_view searchText) noexcept;
    [[nodiscard]] std::vector<const AssetEntry*> VisibleEntries() const;
    void SetMessageCallback(MessageCallback callback);
    void SetOpenVoxCallback(OpenVoxCallback callback);

private:
    struct PendingEntryOperation final
    {
        std::filesystem::path AssetsRoot;
        std::filesystem::path RelativePath;
        std::string Name;
        AssetEntryType Type = AssetEntryType::File;
    };

    struct VoxModelReport final
    {
        std::uint32_t X = 0;
        std::uint32_t Y = 0;
        std::uint32_t Z = 0;
        std::uint32_t VoxelCount = 0;
    };

    struct VoxInspectionReport final
    {
        std::filesystem::path RelativePath;
        bool Succeeded = false;
        std::string Message;
        std::uint32_t Version = 0;
        std::vector<VoxModelReport> Models;
        std::uint64_t TotalVoxelCount = 0;
        bool HasCustomPalette = false;
        std::vector<std::string> Warnings;
    };

    void DrawToolbar();
    void DrawEntries();
    void DrawGrid(
        const std::vector<const AssetEntry*>& entries,
        std::optional<std::filesystem::path>& directoryToEnter);
    void DrawList(
        const std::vector<const AssetEntry*>& entries,
        std::optional<std::filesystem::path>& directoryToEnter);
    void DrawEntryContextMenu(
        const AssetEntry& entry,
        std::optional<std::filesystem::path>& directoryToEnter);
    void DrawSelection() const;
    void DrawStatusMessage() const;
    void DrawBackgroundContextMenu();
    void DrawNewFolderPopup();
    void DrawRenamePopup();
    void DrawDeletePopup();
    void DrawVoxInspectionPopup();
    void RequestNewFolder();
    void RequestRename(const AssetEntry& entry);
    void RequestDelete(const AssetEntry& entry);
    void InspectVox(const AssetEntry& entry);
    void EmitMessage(std::string message) const;
    void ResetPendingOperations() noexcept;
    [[nodiscard]] bool IsPendingOperationCurrent(
        const PendingEntryOperation& operation) const;
    void SynchronizeSelection();
    void SetError(std::string error);
    void SetStatus(std::string message);

    AssetDirectory directory_;
    AssetBrowserViewModel viewModel_;
    MessageCallback messageCallback_;
    OpenVoxCallback openVoxCallback_;
    std::optional<std::filesystem::path> selectedRelativePath_;
    std::optional<std::filesystem::path> newFolderAssetsRoot_;
    std::optional<PendingEntryOperation> pendingRename_;
    std::optional<PendingEntryOperation> pendingDelete_;
    std::optional<VoxInspectionReport> voxInspectionReport_;
    std::array<char, 128> newFolderName_{};
    std::array<char, 256> renameName_{};
    std::string error_;
    std::string statusMessage_;
    bool openNewFolderPopup_ = false;
    bool openRenamePopup_ = false;
    bool openDeletePopup_ = false;
    bool openVoxInspectionPopup_ = false;
    std::size_t refreshCount_ = 0U;
};

} // namespace VoxelForge::Editor
