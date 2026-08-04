#pragma once

#include "VoxelStamps/Library/StampAssetCache.h"
#include "VoxelStamps/Library/StampCatalogService.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class ForgeLibraryDisplayMode : std::uint8_t
{
    Grid,
    List
};

enum class ForgeLibrarySortMode : std::uint8_t
{
    Name,
    Date,
    Type
};

enum class ForgeLibraryScope : std::uint8_t
{
    Project,
    My
};

enum class ForgeLibrarySection : std::uint8_t
{
    Assets,
    Creations,
    Brushes,
    Favorites,
    Recent
};

inline constexpr std::array<ForgeLibrarySection, 5U>
    ForgeLibraryNavigationSections{
        ForgeLibrarySection::Assets,
        ForgeLibrarySection::Creations,
        ForgeLibrarySection::Brushes,
        ForgeLibrarySection::Favorites,
        ForgeLibrarySection::Recent};

enum class ForgeLibraryEmptyState : std::uint8_t
{
    None,
    NoProject,
    EmptyProject,
    NoSearchResults,
    FavoritesEmpty,
    RecentEmpty,
    BrushesEmpty,
    UserLibraryUnavailable,
    AssetsManagedExternally,
    CatalogUnavailable
};

enum class ForgeLibraryResponsiveMode : std::uint8_t
{
    CompactList,
    List,
    Grid
};

struct ForgeLibraryResponsiveLayout final
{
    ForgeLibraryResponsiveMode Mode = ForgeLibraryResponsiveMode::List;
    std::size_t GridColumns = 0U;
    bool UseButtonVisible = false;
    bool UseButtonFullWidth = false;
};

/// Resolves the presentation only. It contains no ImGui state and is kept
/// deterministic so responsive behavior can be covered by unit tests.
[[nodiscard]] ForgeLibraryResponsiveLayout ResolveForgeLibraryResponsiveLayout(
    float availableWidth,
    ForgeLibraryDisplayMode requestedMode,
    bool hasSelection) noexcept;

struct ForgeLibraryThumbnailPoint final
{
    float X = 0.0F;
    float Y = 0.0F;
    StampColor Color{};
};

/// Cached, normalized projection derived from the authoritative Stamp. The
/// panel can render it at any card/detail size without rescanning voxel data.
struct ForgeLibraryThumbnail final
{
    std::vector<ForgeLibraryThumbnailPoint> Points;
    std::uint64_t SourceVoxelCount = 0U;

    [[nodiscard]] bool Available() const noexcept { return !Points.empty(); }
};

struct ForgeLibraryItem final
{
    StampCatalogEntry CatalogEntry;
    std::string DisplayName;
    std::string Category;
    bool Favorite = false;
    bool Recent = false;

    [[nodiscard]] bool operator==(const ForgeLibraryItem&) const noexcept = default;
};

struct ForgeLibrarySelectionDetails final
{
    std::string Name;
    StampDimensions Dimensions{};
    std::uint64_t VoxelCount = 0U;
    std::uint32_t PaletteCount = 0U;
    bool PreviewAvailable = false;
    bool SourceAvailable = false;
    bool Favorite = false;
    StampLibraryScope Scope = StampLibraryScope::Project;
    std::string Category;
};

struct ForgeLibraryOperationResult final
{
    bool Succeeded = false;
    bool SessionActivated = false;
    std::string Message;
};

/// UI-independent presentation model for the Project Forge Library. It owns no
/// placement rules: catalogue discovery is delegated to StampCatalogService,
/// decoded source reuse to StampAssetCache and preview activation to the
/// existing StampPlacementSession.
class ForgeLibraryViewModel final
{
public:
    ForgeLibraryViewModel(
        StampCatalogService& catalogue,
        StampAssetCache& assetCache,
        StampPlacementSession& placementSession);
    ForgeLibraryViewModel(
        StampCatalogService& projectCatalogue,
        StampCatalogService& userCatalogue,
        StampAssetCache& assetCache,
        StampPlacementSession& placementSession);

    [[nodiscard]] ForgeLibraryOperationResult Refresh();
    [[nodiscard]] ForgeLibraryOperationResult RebuildActiveCatalogue();
    void ResetForProjectChange() noexcept;

    void SetSearchText(std::string text);
    [[nodiscard]] const std::string& SearchText() const noexcept;
    void SetDisplayMode(ForgeLibraryDisplayMode mode) noexcept;
    [[nodiscard]] ForgeLibraryDisplayMode DisplayMode() const noexcept;
    void SetSortMode(ForgeLibrarySortMode mode);
    [[nodiscard]] ForgeLibrarySortMode SortMode() const noexcept;
    void SetScope(ForgeLibraryScope scope);
    [[nodiscard]] ForgeLibraryScope Scope() const noexcept;
    [[nodiscard]] bool ScopeAvailable(ForgeLibraryScope scope) const noexcept;
    void SetSection(ForgeLibrarySection section);
    [[nodiscard]] ForgeLibrarySection Section() const noexcept;
    void SetCategory(std::string category);
    [[nodiscard]] const std::string& Category() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Categories() const noexcept;
    [[nodiscard]] bool ToggleFavorite(const Core::UUID& id);
    [[nodiscard]] bool IsFavorite(const Core::UUID& id) const noexcept;

    [[nodiscard]] bool Select(const Core::UUID& id);
    void ClearSelection() noexcept;
    [[nodiscard]] const Core::UUID* SelectedId() const noexcept;
    [[nodiscard]] const ForgeLibrarySelectionDetails* SelectedDetails() const noexcept;
    [[nodiscard]] const VoxelStamp* SelectedPreviewStamp() const noexcept;
    [[nodiscard]] const ForgeLibraryThumbnail* ThumbnailFor(
        const Core::UUID& id) const noexcept;
    [[nodiscard]] const ForgeLibraryThumbnail* SelectedThumbnail() const noexcept;
    [[nodiscard]] std::size_t ThumbnailBuildCount() const noexcept;
    [[nodiscard]] ForgeLibraryEmptyState EmptyState() const noexcept;

    [[nodiscard]] ForgeLibraryOperationResult ActivateSelected(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel = 0U);

    [[nodiscard]] const std::vector<ForgeLibraryItem>& Items() const noexcept;
    [[nodiscard]] const std::string& StatusMessage() const noexcept;
    [[nodiscard]] bool NeedsRefresh() const noexcept;

private:
    using TrackedIdentity =
        std::pair<StampLibraryScope, std::uint64_t>;

    void SortItems();
    void ApplySectionAndCategoryFilters();
    void ReconcileSelection();
    void EnsureThumbnail(const ForgeLibraryItem& item);
    void ReconcileThumbnailCache();
    [[nodiscard]] const ForgeLibraryItem* FindItem(const Core::UUID& id) const noexcept;
    [[nodiscard]] ForgeLibraryItem* FindItem(const Core::UUID& id) noexcept;
    [[nodiscard]] StampCatalogService* ActiveCatalogue() noexcept;
    [[nodiscard]] const StampCatalogService* ActiveCatalogue() const noexcept;
    [[nodiscard]] StampLibraryScope ActiveLibraryScope() const noexcept;
    [[nodiscard]] TrackedIdentity IdentityFor(const Core::UUID& id) const noexcept;
    [[nodiscard]] static bool ContainsIdentity(
        const std::vector<TrackedIdentity>& identities,
        const TrackedIdentity& identity) noexcept;
    void RecordRecent(const StampAssetReference& reference);

    struct ThumbnailCacheEntry final
    {
        std::string ContentHash;
        std::shared_ptr<const ForgeLibraryThumbnail> Thumbnail;
    };

    StampCatalogService& projectCatalogue_;
    StampCatalogService* userCatalogue_ = nullptr;
    StampAssetCache& assetCache_;
    StampPlacementSession& placementSession_;
    std::vector<ForgeLibraryItem> items_;
    std::map<TrackedIdentity, ThumbnailCacheEntry> thumbnailCache_;
    std::vector<TrackedIdentity> favorites_;
    std::vector<TrackedIdentity> recent_;
    std::optional<Core::UUID> selectedId_;
    std::shared_ptr<const VoxelStamp> selectedStamp_;
    std::optional<ForgeLibrarySelectionDetails> selectedDetails_;
    std::string searchText_;
    std::string category_;
    std::vector<std::string> categories_;
    std::string statusMessage_;
    ForgeLibraryDisplayMode displayMode_ = ForgeLibraryDisplayMode::Grid;
    ForgeLibrarySortMode sortMode_ = ForgeLibrarySortMode::Name;
    ForgeLibraryScope scope_ = ForgeLibraryScope::Project;
    ForgeLibrarySection section_ = ForgeLibrarySection::Creations;
    ForgeLibraryEmptyState emptyState_ = ForgeLibraryEmptyState::None;
    std::size_t thumbnailBuildCount_ = 0U;
    bool needsRefresh_ = true;
};

} // namespace VoxelForge::Editor::Stamps
