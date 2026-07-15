#pragma once

#include "AssetEntry.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor
{

enum class AssetBrowserDisplayMode
{
    Grid,
    List
};

enum class AssetBrowserFilter
{
    All,
    Folders,
    Voxel,
    Models,
    Images,
    Text,
    Other
};

enum class AssetBrowserSortMode
{
    Name,
    Type,
    Size,
    Modified
};

enum class AssetDisplayCategory
{
    Folder,
    Voxel,
    Model,
    Image,
    Text,
    Other
};

struct AssetBrowserViewSettings final
{
    AssetBrowserDisplayMode DisplayMode = AssetBrowserDisplayMode::Grid;
    AssetBrowserFilter Filter = AssetBrowserFilter::All;
    AssetBrowserSortMode SortMode = AssetBrowserSortMode::Name;
    bool SortAscending = true;
    float GridCellSize = 112.0F;
    std::array<char, 256> SearchText{};
};

class AssetBrowserViewModel final
{
public:
    [[nodiscard]] AssetBrowserViewSettings& Settings() noexcept;
    [[nodiscard]] const AssetBrowserViewSettings& Settings() const noexcept;

    void SetSearchText(std::string_view searchText) noexcept;
    void ClearSearch() noexcept;
    void OnProjectChanged() noexcept;

    [[nodiscard]] std::string_view SearchText() const noexcept;
    [[nodiscard]] std::vector<const AssetEntry*> VisibleEntries(
        const std::vector<AssetEntry>& entries) const;

private:
    AssetBrowserViewSettings settings_;
};

[[nodiscard]] AssetDisplayCategory ClassifyAssetEntry(
    const AssetEntry& entry);
[[nodiscard]] std::string_view AssetEntryMarker(
    const AssetEntry& entry);

} // namespace VoxelForge::Editor
