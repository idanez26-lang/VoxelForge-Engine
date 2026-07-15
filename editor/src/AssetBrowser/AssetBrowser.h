#pragma once

#include "AssetDirectory.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

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

    [[nodiscard]] const AssetDirectory& Directory() const noexcept;
    [[nodiscard]] const std::optional<std::filesystem::path>&
        SelectedRelativePath() const noexcept;

private:
    void DrawToolbar();
    void DrawEntries();
    void DrawSelection() const;
    void DrawNewFolderPopup();
    void SynchronizeSelection();
    void SetError(std::string error);

    AssetDirectory directory_;
    std::optional<std::filesystem::path> selectedRelativePath_;
    std::array<char, 128> newFolderName_{};
    std::string error_;
    bool openNewFolderPopup_ = false;
};

} // namespace VoxelForge::Editor
