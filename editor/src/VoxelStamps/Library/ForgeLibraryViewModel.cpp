#include "VoxelStamps/Library/ForgeLibraryViewModel.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] std::string LowerAscii(const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

[[nodiscard]] std::string DisplayName(const StampCatalogEntry& entry)
{
    std::string result = entry.FileName;
    constexpr std::string_view extension = ".vfstamp";
    if (result.size() >= extension.size() &&
        LowerAscii(std::string_view(result).substr(result.size() - extension.size())) ==
            extension)
    {
        result.resize(result.size() - extension.size());
    }
    return result.empty() ? std::string("Untitled Stamp") : result;
}

[[nodiscard]] bool NameLess(
    const ForgeLibraryItem& left,
    const ForgeLibraryItem& right)
{
    const std::string leftName = LowerAscii(left.DisplayName);
    const std::string rightName = LowerAscii(right.DisplayName);
    if (leftName != rightName) return leftName < rightName;
    return left.CatalogEntry.Reference.Id.Value() <
        right.CatalogEntry.Reference.Id.Value();
}

[[nodiscard]] std::shared_ptr<const ForgeLibraryThumbnail> BuildThumbnail(
    const VoxelStamp& stamp)
{
    constexpr std::size_t MaximumPreviewPoints = 2048U;
    const auto voxels = stamp.Voxels();
    const auto palette = stamp.Palette();
    if (voxels.empty() || palette.empty()) return {};

    float minimumX = std::numeric_limits<float>::max();
    float minimumY = std::numeric_limits<float>::max();
    float maximumX = std::numeric_limits<float>::lowest();
    float maximumY = std::numeric_limits<float>::lowest();
    for (const StampVoxel& voxel : voxels)
    {
        const float projectedX =
            static_cast<float>(voxel.Position.X - voxel.Position.Z);
        const float projectedY =
            static_cast<float>(voxel.Position.X + voxel.Position.Z) * 0.45F -
            static_cast<float>(voxel.Position.Y);
        minimumX = std::min(minimumX, projectedX);
        minimumY = std::min(minimumY, projectedY);
        maximumX = std::max(maximumX, projectedX);
        maximumY = std::max(maximumY, projectedY);
    }

    const float spanX = std::max(1.0F, maximumX - minimumX);
    const float spanY = std::max(1.0F, maximumY - minimumY);
    const std::size_t stride = std::max<std::size_t>(
        1U, (voxels.size() + MaximumPreviewPoints - 1U) /
            MaximumPreviewPoints);

    auto thumbnail = std::make_shared<ForgeLibraryThumbnail>();
    thumbnail->SourceVoxelCount = static_cast<std::uint64_t>(voxels.size());
    thumbnail->Points.reserve(
        std::min(MaximumPreviewPoints, voxels.size()));
    for (std::size_t index = 0U; index < voxels.size(); index += stride)
    {
        const StampVoxel& voxel = voxels[index];
        if (voxel.LocalColorId >= palette.size()) continue;
        const float projectedX =
            static_cast<float>(voxel.Position.X - voxel.Position.Z);
        const float projectedY =
            static_cast<float>(voxel.Position.X + voxel.Position.Z) * 0.45F -
            static_cast<float>(voxel.Position.Y);
        thumbnail->Points.push_back({
            .X = (projectedX - minimumX) / spanX,
            .Y = (projectedY - minimumY) / spanY,
            .Color = palette[voxel.LocalColorId].Color});
    }
    return thumbnail;
}

} // namespace

ForgeLibraryResponsiveLayout ResolveForgeLibraryResponsiveLayout(
    const float availableWidth,
    const ForgeLibraryDisplayMode requestedMode,
    const bool hasSelection) noexcept
{
    ForgeLibraryResponsiveLayout result{
        .UseButtonVisible = hasSelection,
        .UseButtonFullWidth = hasSelection};
    if (availableWidth < 220.0F)
    {
        result.Mode = ForgeLibraryResponsiveMode::CompactList;
        return result;
    }
    if (requestedMode == ForgeLibraryDisplayMode::List)
    {
        result.Mode = ForgeLibraryResponsiveMode::List;
        return result;
    }

    result.Mode = ForgeLibraryResponsiveMode::Grid;
    if (availableWidth < 320.0F)
        result.GridColumns = 1U;
    else if (availableWidth < 480.0F)
        result.GridColumns = 2U;
    else
        result.GridColumns = std::max<std::size_t>(
            3U, static_cast<std::size_t>(availableWidth / 148.0F));
    return result;
}

ForgeLibraryViewModel::ForgeLibraryViewModel(
    StampCatalogService& catalogue,
    StampAssetCache& assetCache,
    StampPlacementSession& placementSession)
    : catalogue_(catalogue),
      assetCache_(assetCache),
      placementSession_(placementSession)
{
}

ForgeLibraryOperationResult ForgeLibraryViewModel::Refresh()
{
    try
    {
        StampCatalogResult result = catalogue_.Query({.Text = searchText_});
        if (result.Error == StampCatalogError::Missing)
        {
            result = catalogue_.RebuildCatalogue();
            if (result.Succeeded() && !searchText_.empty())
                result = catalogue_.Query({.Text = searchText_});
        }
        if (!result.Succeeded())
        {
            items_.clear();
            ReconcileSelection();
            statusMessage_ = result.Message;
            emptyState_ =
                result.Error == StampCatalogError::NotConfigured ||
                result.Error == StampCatalogError::InvalidProjectRoot
                ? ForgeLibraryEmptyState::NoProject
                : ForgeLibraryEmptyState::CatalogUnavailable;
            needsRefresh_ = false;
            return {.Message = statusMessage_};
        }

        items_.clear();
        if (filter_ != ForgeLibraryFilter::Favorites)
        {
            items_.reserve(result.Catalog.Entries.size());
            for (StampCatalogEntry& entry : result.Catalog.Entries)
            {
                ForgeLibraryItem item;
                item.DisplayName = DisplayName(entry);
                item.CatalogEntry = std::move(entry);
                items_.push_back(std::move(item));
            }
        }
        SortItems();
        for (const ForgeLibraryItem& item : items_) EnsureThumbnail(item);
        if (searchText_.empty() &&
            filter_ != ForgeLibraryFilter::Favorites)
        {
            ReconcileThumbnailCache();
        }
        ReconcileSelection();
        needsRefresh_ = false;
        if (filter_ == ForgeLibraryFilter::Favorites)
            emptyState_ = ForgeLibraryEmptyState::FavoritesEmpty;
        else if (items_.empty() && !searchText_.empty())
            emptyState_ = ForgeLibraryEmptyState::NoSearchResults;
        else if (items_.empty())
            emptyState_ = ForgeLibraryEmptyState::EmptyProject;
        else
            emptyState_ = ForgeLibraryEmptyState::None;
        statusMessage_ = items_.empty()
            ? std::string{}
            : std::to_string(items_.size()) +
                (items_.size() == 1U ? " Stamp" : " Stamps");
        return {.Succeeded = true, .Message = statusMessage_};
    }
    catch (const std::bad_alloc&)
    {
        items_.clear();
        ReconcileSelection();
        needsRefresh_ = false;
        emptyState_ = ForgeLibraryEmptyState::CatalogUnavailable;
        statusMessage_ = "Forge Library ran out of memory while building the view.";
        return {.Message = statusMessage_};
    }
}

void ForgeLibraryViewModel::ResetForProjectChange() noexcept
{
    catalogue_.InvalidateCache();
    static_cast<void>(
        assetCache_.InvalidateScope(StampLibraryScope::Project));
    items_.clear();
    selectedId_.reset();
    selectedStamp_.reset();
    selectedDetails_.reset();
    thumbnailCache_.clear();
    searchText_.clear();
    statusMessage_.clear();
    filter_ = ForgeLibraryFilter::All;
    sortMode_ = ForgeLibrarySortMode::Name;
    emptyState_ = ForgeLibraryEmptyState::None;
    // Grid/List is an artist preference and intentionally survives project changes.
    needsRefresh_ = true;
}

void ForgeLibraryViewModel::SetSearchText(std::string text)
{
    if (searchText_ == text) return;
    searchText_ = std::move(text);
    needsRefresh_ = true;
}

const std::string& ForgeLibraryViewModel::SearchText() const noexcept
{
    return searchText_;
}

void ForgeLibraryViewModel::SetDisplayMode(
    const ForgeLibraryDisplayMode mode) noexcept
{
    displayMode_ = mode;
}

ForgeLibraryDisplayMode ForgeLibraryViewModel::DisplayMode() const noexcept
{
    return displayMode_;
}

void ForgeLibraryViewModel::SetSortMode(const ForgeLibrarySortMode mode)
{
    if (sortMode_ == mode) return;
    sortMode_ = mode;
    SortItems();
}

ForgeLibrarySortMode ForgeLibraryViewModel::SortMode() const noexcept
{
    return sortMode_;
}

void ForgeLibraryViewModel::SetFilter(const ForgeLibraryFilter filter)
{
    if (filter_ == filter) return;
    filter_ = filter;
    needsRefresh_ = true;
}

ForgeLibraryFilter ForgeLibraryViewModel::Filter() const noexcept
{
    return filter_;
}

bool ForgeLibraryViewModel::Select(const Core::UUID& id)
{
    const ForgeLibraryItem* const item = FindItem(id);
    if (item == nullptr)
    {
        ClearSelection();
        statusMessage_ = "The selected Stamp is no longer in this Forge Library view.";
        return false;
    }

    const StampAssetCacheResult loaded =
        assetCache_.GetOrLoad(item->CatalogEntry.Reference);
    if (!loaded.Succeeded())
    {
        selectedId_ = id;
        selectedStamp_.reset();
        selectedDetails_ = ForgeLibrarySelectionDetails{
            .Name = item->DisplayName,
            .Dimensions = item->CatalogEntry.Dimensions,
            .VoxelCount = item->CatalogEntry.VoxelCount,
            .PaletteCount = item->CatalogEntry.PaletteCount};
        statusMessage_ = loaded.Message;
        return false;
    }

    selectedId_ = id;
    selectedStamp_ = loaded.Stamp;
    selectedDetails_ = ForgeLibrarySelectionDetails{
        .Name = item->DisplayName,
        .Dimensions = item->CatalogEntry.Dimensions,
        .VoxelCount = item->CatalogEntry.VoxelCount,
        .PaletteCount = item->CatalogEntry.PaletteCount,
        .PreviewAvailable = ThumbnailFor(id) != nullptr};
    statusMessage_ = "Stamp selected. Double-click it or choose Use Stamp.";
    return true;
}

void ForgeLibraryViewModel::ClearSelection() noexcept
{
    selectedId_.reset();
    selectedStamp_.reset();
    selectedDetails_.reset();
}

const Core::UUID* ForgeLibraryViewModel::SelectedId() const noexcept
{
    return selectedId_ ? &*selectedId_ : nullptr;
}

const ForgeLibrarySelectionDetails*
ForgeLibraryViewModel::SelectedDetails() const noexcept
{
    return selectedDetails_ ? &*selectedDetails_ : nullptr;
}

const VoxelStamp* ForgeLibraryViewModel::SelectedPreviewStamp() const noexcept
{
    return selectedStamp_.get();
}

const ForgeLibraryThumbnail* ForgeLibraryViewModel::ThumbnailFor(
    const Core::UUID& id) const noexcept
{
    const auto thumbnail = thumbnailCache_.find(id.Value());
    return thumbnail == thumbnailCache_.end() ||
            !thumbnail->second.Thumbnail ||
            !thumbnail->second.Thumbnail->Available()
        ? nullptr
        : thumbnail->second.Thumbnail.get();
}

const ForgeLibraryThumbnail*
ForgeLibraryViewModel::SelectedThumbnail() const noexcept
{
    return selectedId_ ? ThumbnailFor(*selectedId_) : nullptr;
}

std::size_t ForgeLibraryViewModel::ThumbnailBuildCount() const noexcept
{
    return thumbnailBuildCount_;
}

ForgeLibraryEmptyState ForgeLibraryViewModel::EmptyState() const noexcept
{
    return emptyState_;
}

ForgeLibraryOperationResult ForgeLibraryViewModel::ActivateSelected(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t targetSubModel)
{
    if (!selectedId_)
        return {.Message = "Select a Stamp before choosing Use."};

    const ForgeLibraryItem* const item = FindItem(*selectedId_);
    if (item == nullptr)
        return {.Message = "The selected Stamp is no longer available."};

    const StampAssetCacheResult loaded =
        assetCache_.GetOrLoad(item->CatalogEntry.Reference);
    if (!loaded.Succeeded())
        return {.Message = loaded.Message};

    const StampFixedPoint initialTarget = loaded.Stamp->Pivot().LocalPosition;
    const StampPlacementSessionResult activated = placementSession_.SelectAsset(
        loaded.Stamp.get(), document, documentGeneration,
        targetSubModel, initialTarget);
    if (!activated.Succeeded)
    {
        return {
            .Message = std::string(StampPlacementDiagnosticMessage(
                activated.Diagnostic))};
    }

    statusMessage_ = "Stamp placement active. Move in the viewport, click to place, Esc to finish.";
    return {
        .Succeeded = true,
        .SessionActivated = true,
        .Message = statusMessage_};
}

const std::vector<ForgeLibraryItem>&
ForgeLibraryViewModel::Items() const noexcept
{
    return items_;
}

const std::string& ForgeLibraryViewModel::StatusMessage() const noexcept
{
    return statusMessage_;
}

bool ForgeLibraryViewModel::NeedsRefresh() const noexcept
{
    return needsRefresh_;
}

void ForgeLibraryViewModel::SortItems()
{
    // Date/type metadata is deliberately not synthesized in V1. Those modes
    // are stable, prepared ordering contracts and fall back to name until the
    // authoritative catalogue gains the corresponding derived fields.
    std::stable_sort(items_.begin(), items_.end(), NameLess);
}

void ForgeLibraryViewModel::ReconcileSelection()
{
    if (!selectedId_) return;
    if (FindItem(*selectedId_) == nullptr) ClearSelection();
}

void ForgeLibraryViewModel::EnsureThumbnail(const ForgeLibraryItem& item)
{
    const std::uint64_t key = item.CatalogEntry.Reference.Id.Value();
    const auto existing = thumbnailCache_.find(key);
    if (existing != thumbnailCache_.end() &&
        existing->second.ContentHash ==
            item.CatalogEntry.Reference.ContentHash)
    {
        return;
    }

    const StampAssetCacheResult loaded =
        assetCache_.GetOrLoad(item.CatalogEntry.Reference);
    if (!loaded.Succeeded())
    {
        thumbnailCache_.erase(key);
        return;
    }
    std::shared_ptr<const ForgeLibraryThumbnail> thumbnail =
        BuildThumbnail(*loaded.Stamp);
    ++thumbnailBuildCount_;
    thumbnailCache_.insert_or_assign(
        key,
        ThumbnailCacheEntry{
            .ContentHash = item.CatalogEntry.Reference.ContentHash,
            .Thumbnail = std::move(thumbnail)});
}

void ForgeLibraryViewModel::ReconcileThumbnailCache()
{
    for (auto cached = thumbnailCache_.begin();
         cached != thumbnailCache_.end();)
    {
        const bool stillPresent = std::any_of(
            items_.begin(),
            items_.end(),
            [id = cached->first](const ForgeLibraryItem& item)
            {
                return item.CatalogEntry.Reference.Id.Value() == id;
            });
        if (!stillPresent)
            cached = thumbnailCache_.erase(cached);
        else
            ++cached;
    }
}

const ForgeLibraryItem* ForgeLibraryViewModel::FindItem(
    const Core::UUID& id) const noexcept
{
    const auto item = std::find_if(items_.begin(), items_.end(),
        [&id](const ForgeLibraryItem& candidate) {
            return candidate.CatalogEntry.Reference.Id == id;
        });
    return item == items_.end() ? nullptr : &*item;
}

} // namespace VoxelForge::Editor::Stamps
