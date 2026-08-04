#include "VoxelStamps/Library/ForgeLibraryViewModel.h"

#include "VoxelStamps/Library/StampLibraryPaths.h"

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

[[nodiscard]] bool EqualsInsensitive(
    const std::string_view left,
    const std::string_view right)
{
    return LowerAscii(left) == LowerAscii(right);
}

[[nodiscard]] std::string CategoryFrom(const StampCatalogEntry& entry)
{
    const std::filesystem::path& root =
        entry.Reference.Scope == StampLibraryScope::Project
        ? ProjectCreationsRelativePath
        : UserCreationsRelativePath;
    const std::filesystem::path relative =
        entry.Reference.RelativePath.lexically_normal().lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return "Uncategorized";
    const auto first = relative.begin();
    if (first != relative.end() && *first == "..")
        return "Uncategorized";
    const std::filesystem::path category = relative.parent_path();
    return category.empty() || category == "."
        ? std::string("Uncategorized")
        : category.generic_string();
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
    : projectCatalogue_(catalogue),
      assetCache_(assetCache),
      placementSession_(placementSession)
{
}

ForgeLibraryViewModel::ForgeLibraryViewModel(
    StampCatalogService& projectCatalogue,
    StampCatalogService& userCatalogue,
    StampAssetCache& assetCache,
    StampPlacementSession& placementSession)
    : projectCatalogue_(projectCatalogue),
      userCatalogue_(&userCatalogue),
      assetCache_(assetCache),
      placementSession_(placementSession)
{
}

ForgeLibraryOperationResult ForgeLibraryViewModel::Refresh()
{
    try
    {
        items_.clear();
        categories_.clear();
        if (section_ == ForgeLibrarySection::Assets)
        {
            ReconcileSelection();
            emptyState_ = ForgeLibraryEmptyState::AssetsManagedExternally;
            statusMessage_ = "Project files remain available in the Assets workspace.";
            needsRefresh_ = false;
            return {.Succeeded = true, .Message = statusMessage_};
        }
        if (section_ == ForgeLibrarySection::Brushes)
        {
            ReconcileSelection();
            emptyState_ = ForgeLibraryEmptyState::BrushesEmpty;
            statusMessage_ = "Brush profiles are separate from reusable creations.";
            needsRefresh_ = false;
            return {.Succeeded = true, .Message = statusMessage_};
        }

        StampCatalogService* const catalogue = ActiveCatalogue();
        if (catalogue == nullptr)
        {
            ReconcileSelection();
            emptyState_ = ForgeLibraryEmptyState::UserLibraryUnavailable;
            statusMessage_ = "My Library is not configured for this session.";
            needsRefresh_ = false;
            return {.Message = statusMessage_};
        }

        StampCatalogResult result = catalogue->Query({.Text = searchText_});
        if (result.Error == StampCatalogError::Missing)
        {
            result = catalogue->RebuildCatalogue();
            if (result.Succeeded() && !searchText_.empty())
                result = catalogue->Query({.Text = searchText_});
        }
        if (!result.Succeeded())
        {
            ReconcileSelection();
            statusMessage_ = result.Message;
            emptyState_ = scope_ == ForgeLibraryScope::My &&
                (result.Error == StampCatalogError::NotConfigured ||
                 result.Error == StampCatalogError::InvalidProjectRoot)
                ? ForgeLibraryEmptyState::UserLibraryUnavailable
                : result.Error == StampCatalogError::NotConfigured ||
                  result.Error == StampCatalogError::InvalidProjectRoot
                ? ForgeLibraryEmptyState::NoProject
                : ForgeLibraryEmptyState::CatalogUnavailable;
            needsRefresh_ = false;
            return {.Message = statusMessage_};
        }

        items_.reserve(result.Catalog.Entries.size());
        for (StampCatalogEntry& entry : result.Catalog.Entries)
        {
            ForgeLibraryItem item;
            item.DisplayName = DisplayName(entry);
            item.Category = CategoryFrom(entry);
            item.Favorite = ContainsIdentity(
                favorites_, {entry.Reference.Scope, entry.Reference.Id.Value()});
            item.Recent = ContainsIdentity(
                recent_, {entry.Reference.Scope, entry.Reference.Id.Value()});
            item.CatalogEntry = std::move(entry);
            categories_.push_back(item.Category);
            items_.push_back(std::move(item));
        }
        std::sort(categories_.begin(), categories_.end(),
            [](const std::string& left, const std::string& right)
            {
                return LowerAscii(left) < LowerAscii(right);
            });
        categories_.erase(
            std::unique(categories_.begin(), categories_.end(),
                [](const std::string& left, const std::string& right)
                {
                    return EqualsInsensitive(left, right);
                }),
            categories_.end());

        ApplySectionAndCategoryFilters();
        SortItems();
        for (const ForgeLibraryItem& item : items_) EnsureThumbnail(item);
        if (searchText_.empty() && category_.empty() &&
            section_ == ForgeLibrarySection::Creations)
        {
            ReconcileThumbnailCache();
        }
        ReconcileSelection();
        needsRefresh_ = false;
        if (items_.empty() && (!searchText_.empty() || !category_.empty()))
            emptyState_ = ForgeLibraryEmptyState::NoSearchResults;
        else if (items_.empty() && section_ == ForgeLibrarySection::Favorites)
            emptyState_ = ForgeLibraryEmptyState::FavoritesEmpty;
        else if (items_.empty() && section_ == ForgeLibrarySection::Recent)
            emptyState_ = ForgeLibraryEmptyState::RecentEmpty;
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

ForgeLibraryOperationResult ForgeLibraryViewModel::RebuildActiveCatalogue()
{
    StampCatalogService* const catalogue = ActiveCatalogue();
    if (catalogue == nullptr)
        return Refresh();
    catalogue->InvalidateCache();
    const StampCatalogResult rebuilt = catalogue->RebuildCatalogue();
    if (!rebuilt.Succeeded())
    {
        needsRefresh_ = false;
        statusMessage_ = rebuilt.Message;
        emptyState_ = scope_ == ForgeLibraryScope::My &&
            (rebuilt.Error == StampCatalogError::NotConfigured ||
             rebuilt.Error == StampCatalogError::InvalidProjectRoot)
            ? ForgeLibraryEmptyState::UserLibraryUnavailable
            : rebuilt.Error == StampCatalogError::NotConfigured ||
              rebuilt.Error == StampCatalogError::InvalidProjectRoot
            ? ForgeLibraryEmptyState::NoProject
            : ForgeLibraryEmptyState::CatalogUnavailable;
        return {.Message = statusMessage_};
    }
    needsRefresh_ = true;
    return Refresh();
}

void ForgeLibraryViewModel::ResetForProjectChange() noexcept
{
    projectCatalogue_.InvalidateCache();
    static_cast<void>(
        assetCache_.InvalidateScope(StampLibraryScope::Project));
    items_.clear();
    selectedId_.reset();
    selectedStamp_.reset();
    selectedDetails_.reset();
    std::erase_if(thumbnailCache_,
        [](const auto& entry)
        {
            return entry.first.first == StampLibraryScope::Project;
        });
    std::erase_if(favorites_,
        [](const TrackedIdentity& identity)
        {
            return identity.first == StampLibraryScope::Project;
        });
    std::erase_if(recent_,
        [](const TrackedIdentity& identity)
        {
            return identity.first == StampLibraryScope::Project;
        });
    searchText_.clear();
    category_.clear();
    categories_.clear();
    statusMessage_.clear();
    scope_ = ForgeLibraryScope::Project;
    section_ = ForgeLibrarySection::Creations;
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

void ForgeLibraryViewModel::SetScope(const ForgeLibraryScope scope)
{
    if (scope_ == scope) return;
    scope_ = scope;
    category_.clear();
    categories_.clear();
    ClearSelection();
    needsRefresh_ = true;
}

ForgeLibraryScope ForgeLibraryViewModel::Scope() const noexcept
{
    return scope_;
}

bool ForgeLibraryViewModel::ScopeAvailable(
    const ForgeLibraryScope scope) const noexcept
{
    return scope == ForgeLibraryScope::Project || userCatalogue_ != nullptr;
}

void ForgeLibraryViewModel::SetSection(const ForgeLibrarySection section)
{
    if (section_ == section) return;
    section_ = section;
    category_.clear();
    ClearSelection();
    needsRefresh_ = true;
}

ForgeLibrarySection ForgeLibraryViewModel::Section() const noexcept
{
    return section_;
}

void ForgeLibraryViewModel::SetCategory(std::string category)
{
    if (category_ == category) return;
    category_ = std::move(category);
    ClearSelection();
    needsRefresh_ = true;
}

const std::string& ForgeLibraryViewModel::Category() const noexcept
{
    return category_;
}

const std::vector<std::string>&
ForgeLibraryViewModel::Categories() const noexcept
{
    return categories_;
}

bool ForgeLibraryViewModel::ToggleFavorite(const Core::UUID& id)
{
    ForgeLibraryItem* const item = FindItem(id);
    if (item == nullptr) return false;
    const TrackedIdentity identity{
        item->CatalogEntry.Reference.Scope, id.Value()};
    const auto tracked = std::find(
        favorites_.begin(), favorites_.end(), identity);
    if (tracked == favorites_.end())
    {
        favorites_.push_back(identity);
        item->Favorite = true;
    }
    else
    {
        favorites_.erase(tracked);
        item->Favorite = false;
    }
    if (selectedDetails_ && selectedId_ && *selectedId_ == id)
        selectedDetails_->Favorite = item->Favorite;
    if (section_ == ForgeLibrarySection::Favorites && !item->Favorite)
        needsRefresh_ = true;
    return item->Favorite;
}

bool ForgeLibraryViewModel::IsFavorite(const Core::UUID& id) const noexcept
{
    return ContainsIdentity(favorites_, IdentityFor(id));
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
            .PaletteCount = item->CatalogEntry.PaletteCount,
            .Favorite = item->Favorite,
            .Scope = item->CatalogEntry.Reference.Scope,
            .Category = item->Category};
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
        .PreviewAvailable = ThumbnailFor(id) != nullptr,
        .SourceAvailable = true,
        .Favorite = item->Favorite,
        .Scope = item->CatalogEntry.Reference.Scope,
        .Category = item->Category};
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
    const auto thumbnail = thumbnailCache_.find(IdentityFor(id));
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

    RecordRecent(item->CatalogEntry.Reference);
    if (ForgeLibraryItem* const mutableItem = FindItem(*selectedId_))
        mutableItem->Recent = true;
    statusMessage_ =
        "Stamp Placement active in Tool Options. Use the Move/Rotate Y "
        "gizmo, then press Enter or choose Place Full Stamp; Esc cancels.";
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
    if (section_ == ForgeLibrarySection::Recent)
    {
        std::stable_sort(items_.begin(), items_.end(),
            [this](const ForgeLibraryItem& left,
                   const ForgeLibraryItem& right)
            {
                const TrackedIdentity leftIdentity{
                    left.CatalogEntry.Reference.Scope,
                    left.CatalogEntry.Reference.Id.Value()};
                const TrackedIdentity rightIdentity{
                    right.CatalogEntry.Reference.Scope,
                    right.CatalogEntry.Reference.Id.Value()};
                const auto leftPosition =
                    std::find(recent_.begin(), recent_.end(), leftIdentity);
                const auto rightPosition =
                    std::find(recent_.begin(), recent_.end(), rightIdentity);
                return leftPosition < rightPosition;
            });
        return;
    }
    if (sortMode_ == ForgeLibrarySortMode::Type)
    {
        std::stable_sort(items_.begin(), items_.end(),
            [](const ForgeLibraryItem& left,
               const ForgeLibraryItem& right)
            {
                const std::string leftCategory = LowerAscii(left.Category);
                const std::string rightCategory = LowerAscii(right.Category);
                return leftCategory == rightCategory
                    ? NameLess(left, right)
                    : leftCategory < rightCategory;
            });
        return;
    }
    // Date metadata is not synthesized in V1; it falls back to stable name.
    std::stable_sort(items_.begin(), items_.end(), NameLess);
}

void ForgeLibraryViewModel::ApplySectionAndCategoryFilters()
{
    items_.erase(
        std::remove_if(items_.begin(), items_.end(),
            [this](const ForgeLibraryItem& item)
            {
                if (!category_.empty() &&
                    !EqualsInsensitive(item.Category, category_))
                    return true;
                if (section_ == ForgeLibrarySection::Favorites)
                    return !item.Favorite;
                if (section_ == ForgeLibrarySection::Recent)
                    return !item.Recent;
                return false;
            }),
        items_.end());
}

void ForgeLibraryViewModel::ReconcileSelection()
{
    if (!selectedId_) return;
    if (FindItem(*selectedId_) == nullptr) ClearSelection();
}

void ForgeLibraryViewModel::EnsureThumbnail(const ForgeLibraryItem& item)
{
    const TrackedIdentity key{
        item.CatalogEntry.Reference.Scope,
        item.CatalogEntry.Reference.Id.Value()};
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
        if (cached->first.first != ActiveLibraryScope())
        {
            ++cached;
            continue;
        }
        const bool stillPresent = std::any_of(
            items_.begin(),
            items_.end(),
            [identity = cached->first](const ForgeLibraryItem& item)
            {
                return item.CatalogEntry.Reference.Scope == identity.first &&
                    item.CatalogEntry.Reference.Id.Value() == identity.second;
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

ForgeLibraryItem* ForgeLibraryViewModel::FindItem(
    const Core::UUID& id) noexcept
{
    const auto item = std::find_if(items_.begin(), items_.end(),
        [&id](const ForgeLibraryItem& candidate)
        {
            return candidate.CatalogEntry.Reference.Id == id;
        });
    return item == items_.end() ? nullptr : &*item;
}

StampCatalogService* ForgeLibraryViewModel::ActiveCatalogue() noexcept
{
    return scope_ == ForgeLibraryScope::Project
        ? &projectCatalogue_
        : userCatalogue_;
}

const StampCatalogService*
ForgeLibraryViewModel::ActiveCatalogue() const noexcept
{
    return scope_ == ForgeLibraryScope::Project
        ? &projectCatalogue_
        : userCatalogue_;
}

StampLibraryScope ForgeLibraryViewModel::ActiveLibraryScope() const noexcept
{
    return scope_ == ForgeLibraryScope::Project
        ? StampLibraryScope::Project
        : StampLibraryScope::User;
}

ForgeLibraryViewModel::TrackedIdentity ForgeLibraryViewModel::IdentityFor(
    const Core::UUID& id) const noexcept
{
    return {ActiveLibraryScope(), id.Value()};
}

bool ForgeLibraryViewModel::ContainsIdentity(
    const std::vector<TrackedIdentity>& identities,
    const TrackedIdentity& identity) noexcept
{
    return std::find(identities.begin(), identities.end(), identity) !=
        identities.end();
}

void ForgeLibraryViewModel::RecordRecent(
    const StampAssetReference& reference)
{
    constexpr std::size_t MaximumRecent = 24U;
    const TrackedIdentity identity{
        reference.Scope, reference.Id.Value()};
    recent_.erase(
        std::remove(recent_.begin(), recent_.end(), identity),
        recent_.end());
    recent_.insert(recent_.begin(), identity);
    if (recent_.size() > MaximumRecent)
        recent_.resize(MaximumRecent);
}

} // namespace VoxelForge::Editor::Stamps
