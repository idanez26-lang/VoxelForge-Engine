#include "AssetBrowserViewModel.h"

#include <algorithm>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

namespace
{
std::string FoldAscii(const std::string_view text)
{
    std::string folded;
    folded.reserve(text.size());

    for (const char character : text)
    {
        folded.push_back(
            character >= 'A' && character <= 'Z'
                ? static_cast<char>(character + ('a' - 'A'))
                : character);
    }

    return folded;
}

bool ContainsAsciiCaseInsensitive(
    const std::string_view text,
    const std::string_view query)
{
    if (query.empty())
    {
        return true;
    }

    return FoldAscii(text).find(FoldAscii(query)) != std::string::npos;
}

bool MatchesFilter(
    const AssetEntry& entry,
    const AssetBrowserFilter filter) noexcept
{
    if (filter == AssetBrowserFilter::All)
    {
        return true;
    }

    const AssetDisplayCategory category = ClassifyAssetEntry(entry);

    switch (filter)
    {
    case AssetBrowserFilter::Folders:
        return category == AssetDisplayCategory::Folder;
    case AssetBrowserFilter::Voxel:
        return category == AssetDisplayCategory::Voxel;
    case AssetBrowserFilter::Models:
        return category == AssetDisplayCategory::Model;
    case AssetBrowserFilter::Images:
        return category == AssetDisplayCategory::Image;
    case AssetBrowserFilter::Text:
        return category == AssetDisplayCategory::Text;
    case AssetBrowserFilter::Other:
        return category == AssetDisplayCategory::Other;
    case AssetBrowserFilter::All:
        return true;
    }

    return false;
}

template<typename Value>
int CompareOptional(
    const std::optional<Value>& left,
    const std::optional<Value>& right)
{
    if (left && right)
    {
        if (*left < *right)
        {
            return -1;
        }

        if (*right < *left)
        {
            return 1;
        }

        return 0;
    }

    if (left)
    {
        return -1;
    }

    if (right)
    {
        return 1;
    }

    return 0;
}

int CompareNames(const AssetEntry& left, const AssetEntry& right)
{
    const std::string leftName = FoldAscii(left.Name());
    const std::string rightName = FoldAscii(right.Name());

    if (leftName < rightName)
    {
        return -1;
    }

    if (rightName < leftName)
    {
        return 1;
    }

    if (left.Name() < right.Name())
    {
        return -1;
    }

    if (right.Name() < left.Name())
    {
        return 1;
    }

    const std::string leftPath = left.RelativePath().generic_string();
    const std::string rightPath = right.RelativePath().generic_string();
    return leftPath < rightPath ? -1 : rightPath < leftPath ? 1 : 0;
}

int ComparePrimary(
    const AssetEntry& left,
    const AssetEntry& right,
    const AssetBrowserSortMode sortMode)
{
    switch (sortMode)
    {
    case AssetBrowserSortMode::Name:
        return CompareNames(left, right);
    case AssetBrowserSortMode::Type:
    {
        const auto leftCategory = ClassifyAssetEntry(left);
        const auto rightCategory = ClassifyAssetEntry(right);
        return leftCategory < rightCategory
            ? -1
            : rightCategory < leftCategory ? 1 : 0;
    }
    case AssetBrowserSortMode::Size:
        return CompareOptional(left.FileSize(), right.FileSize());
    case AssetBrowserSortMode::Modified:
        return CompareOptional(left.LastWriteTime(), right.LastWriteTime());
    }

    return 0;
}

bool EntryLess(
    const AssetEntry* left,
    const AssetEntry* right,
    const AssetBrowserViewSettings& settings)
{
    if (left->IsDirectory() != right->IsDirectory())
    {
        return left->IsDirectory();
    }

    int comparison = ComparePrimary(*left, *right, settings.SortMode);

    if (comparison == 0)
    {
        comparison = CompareNames(*left, *right);
    }

    if (comparison == 0)
    {
        return false;
    }

    return settings.SortAscending ? comparison < 0 : comparison > 0;
}
}

AssetBrowserViewSettings& AssetBrowserViewModel::Settings() noexcept
{
    return settings_;
}

const AssetBrowserViewSettings& AssetBrowserViewModel::Settings() const noexcept
{
    return settings_;
}

void AssetBrowserViewModel::SetSearchText(
    const std::string_view searchText) noexcept
{
    settings_.SearchText.fill('\0');
    const std::size_t count = std::min(
        searchText.size(),
        settings_.SearchText.size() - 1U);

    if (count > 0U)
    {
        std::copy_n(searchText.data(), count, settings_.SearchText.data());
    }
}

void AssetBrowserViewModel::ClearSearch() noexcept
{
    settings_.SearchText.fill('\0');
}

void AssetBrowserViewModel::OnProjectChanged() noexcept
{
    ClearSearch();
}

std::string_view AssetBrowserViewModel::SearchText() const noexcept
{
    return settings_.SearchText.data();
}

std::vector<const AssetEntry*> AssetBrowserViewModel::VisibleEntries(
    const std::vector<AssetEntry>& entries) const
{
    std::vector<const AssetEntry*> visibleEntries;
    visibleEntries.reserve(entries.size());

    for (const AssetEntry& entry : entries)
    {
        if (MatchesFilter(entry, settings_.Filter) &&
            ContainsAsciiCaseInsensitive(entry.Name(), SearchText()))
        {
            visibleEntries.push_back(&entry);
        }
    }

    std::sort(
        visibleEntries.begin(),
        visibleEntries.end(),
        [this](const AssetEntry* left, const AssetEntry* right)
        {
            return EntryLess(left, right, settings_);
        });
    return visibleEntries;
}

AssetDisplayCategory ClassifyAssetEntry(const AssetEntry& entry)
{
    if (entry.IsDirectory())
    {
        return AssetDisplayCategory::Folder;
    }

    const std::string extension = FoldAscii(entry.Extension());

    if (extension == ".vox" || extension == ".vfvoxel" ||
        extension == ".qb")
    {
        return AssetDisplayCategory::Voxel;
    }

    if (extension == ".obj")
    {
        return AssetDisplayCategory::Model;
    }

    if (extension == ".png" || extension == ".jpg" ||
        extension == ".jpeg" || extension == ".bmp" ||
        extension == ".tga")
    {
        return AssetDisplayCategory::Image;
    }

    if (extension == ".txt" || extension == ".md" ||
        extension == ".json" || extension == ".ini")
    {
        return AssetDisplayCategory::Text;
    }

    return AssetDisplayCategory::Other;
}

std::string_view AssetEntryMarker(const AssetEntry& entry)
{
    if (entry.IsDirectory())
    {
        return "[DIR]";
    }

    const std::string extension = FoldAscii(entry.Extension());

    if (extension == ".vox")
    {
        return "[VOX]";
    }

    if (extension == ".vfvoxel")
    {
        return "[VFVOX]";
    }

    if (extension == ".obj")
    {
        return "[OBJ]";
    }

    if (extension == ".qb")
    {
        return "[QB]";
    }

    const AssetDisplayCategory category = ClassifyAssetEntry(entry);

    if (category == AssetDisplayCategory::Image)
    {
        return "[IMG]";
    }

    if (category == AssetDisplayCategory::Text)
    {
        return "[TXT]";
    }

    return "[FILE]";
}

} // namespace VoxelForge::Editor
