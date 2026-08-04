#include "VoxelStamps/Library/ForgeLibraryViewModel.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {16U, 16U, 16U}});
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "forge-library-tests.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Forge Library test document must build.");
    return std::move(*loaded.Document);
}

[[nodiscard]] VoxelStamp MakeStamp(
    const std::uint64_t id,
    const std::uint32_t width,
    const std::uint8_t red)
{
    std::vector<StampVoxel> voxels;
    for (std::uint32_t x = 0U; x < width; ++x)
    {
        voxels.push_back({
            .Position = {.X = static_cast<std::int32_t>(x)},
            .LocalColorId = 0U});
    }
    StampValidationResult validation{};
    auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = {}},
        {.Minimum = {},
         .Maximum = {.X = static_cast<std::int32_t>(width - 1U)},
         .Dimensions = {.X = width, .Y = 1U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Corner,
         .ResolvedMode = StampPivotMode::Corner,
         .LocalPosition = {}},
        {},
        {{.LocalColorId = 0U,
          .Color = {.Red = red, .Green = 20U, .Blue = 30U, .Alpha = 255U}}},
        std::move(voxels), DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value(), validation.Message);
    return std::move(*stamp);
}

struct Source final
{
    StampAssetReference Reference;
    VoxelStamp Stamp;
    std::uintmax_t Bytes = 0U;
};

class MemoryRepository final : public IStampLibraryRepository
{
public:
    std::vector<Source> Sources;

    void Add(
        const std::uint64_t id,
        const std::string& name,
        const std::uint32_t width,
        const std::uint8_t red,
        const std::string& category = {},
        const StampLibraryScope scope = StampLibraryScope::Project)
    {
        VoxelStamp stamp = MakeStamp(id, width, red);
        stamp = VoxelStamp::TryCreate(
            {.Id = stamp.Identity().Id,
             .ContentHash = "hash-" + std::to_string(id)},
            stamp.Bounds(), stamp.Pivot(), stamp.Transform(),
            {stamp.Palette().begin(), stamp.Palette().end()},
            {stamp.Voxels().begin(), stamp.Voxels().end()})
                    .value();
        Sources.push_back({
            .Reference = {
                .Id = stamp.Identity().Id,
                .ContentHash = stamp.Identity().ContentHash,
                .RelativePath =
                    (scope == StampLibraryScope::Project
                        ? std::filesystem::path(
                              "Assets/ForgeLibrary/Creations")
                        : std::filesystem::path("Library/Creations")) /
                    category / name,
                .Scope = scope},
            .Stamp = std::move(stamp),
            .Bytes = 100U + id});
    }

    [[nodiscard]] StampLibraryResult Install(
        const VoxelStamp&, const StampInstallOptions&) override
    {
        return {.Error = StampLibraryError::InvalidReference};
    }

    [[nodiscard]] StampLibraryResult Read(
        const StampAssetReference& reference) const override
    {
        ++ReadCount;
        const auto source = std::find_if(
            Sources.begin(), Sources.end(),
            [&reference](const Source& candidate) {
                return candidate.Reference == reference;
            });
        if (source == Sources.end())
            return {.Error = StampLibraryError::AssetNotFound,
                    .Message = "Fixture Stamp is missing."};
        return {.Reference = source->Reference, .Stamp = source->Stamp};
    }

    [[nodiscard]] StampLibrarySourceFactsResult InspectSource(
        const StampAssetReference& reference) const override
    {
        const auto source = std::find_if(
            Sources.begin(), Sources.end(),
            [&reference](const Source& candidate) {
                return candidate.Reference == reference;
            });
        if (source == Sources.end())
            return {.Error = StampLibraryError::AssetNotFound,
                    .Message = "Fixture Stamp is missing."};
        return {.Facts = StampLibrarySourceFacts{
                    .FileBytes = source->Bytes,
                    .LastWriteTime =
                        std::filesystem::file_time_type{} +
                        std::filesystem::file_time_type::duration(
                            source->Bytes)}};
    }

    [[nodiscard]] StampLibraryResult EnumerateSourceAssets() const override
    {
        StampLibraryResult result;
        for (const Source& source : Sources)
            result.Assets.push_back({source.Reference, source.Bytes});
        return result;
    }

    [[nodiscard]] StampLibraryResult Remove(
        const StampAssetReference& reference) override
    {
        Sources.erase(
            std::remove_if(
                Sources.begin(), Sources.end(),
                [&reference](const Source& source) {
                    return source.Reference == reference;
                }),
            Sources.end());
        return {};
    }

    [[nodiscard]] StampLibraryResult ResolvePortableReference(
        const std::filesystem::path&) const override
    {
        return {.Error = StampLibraryError::InvalidReference};
    }

    [[nodiscard]] StampLibraryResult RebuildSourceInventory() const override
    {
        return EnumerateSourceAssets();
    }

    mutable std::size_t ReadCount = 0U;
};

class MemoryStore final : public IStampCatalogStore
{
public:
    std::optional<StampCatalog> Catalog;

    [[nodiscard]] StampCatalogResult LoadCatalogue() const override
    {
        if (!Catalog)
            return {.Error = StampCatalogError::Missing,
                    .Message = "Fixture catalogue is missing."};
        return {.Catalog = *Catalog};
    }

    [[nodiscard]] StampCatalogResult WriteCatalogueAtomically(
        const StampCatalog& catalogue) override
    {
        Catalog = catalogue;
        return {.Catalog = catalogue};
    }
};

struct Fixture final
{
    MemoryRepository Repository;
    MemoryRepository UserRepository;
    MemoryStore Store;
    MemoryStore UserStore;
    StampPlacementSession Session;
    StampCatalogService Catalog{Repository, Store};
    StampCatalogService UserCatalog{UserRepository, UserStore};
    StampAssetCache AssetCache{Repository, UserRepository};
    ForgeLibraryViewModel ViewModel{
        Catalog, UserCatalog, AssetCache, Session};
};

void TestEmptyAndCompleteViews()
{
    Fixture fixture;
    const auto empty = fixture.ViewModel.Refresh();
    Require(empty.Succeeded && fixture.ViewModel.Items().empty(),
        "An empty Project Library must be a successful empty view.");
    Require(
        fixture.ViewModel.EmptyState() ==
            ForgeLibraryEmptyState::EmptyProject,
        "An empty Project Library must expose an actionable empty state.");

    fixture.Repository.Add(2U, "Tree.vfstamp", 2U, 20U);
    fixture.Repository.Add(1U, "arch.vfstamp", 3U, 30U);
    fixture.Catalog.InvalidateCache();
    Require(fixture.Catalog.RebuildCatalogue().Succeeded(),
        "Fixture catalogue rebuild must succeed.");
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 2U &&
            fixture.ViewModel.Items()[0].DisplayName == "arch" &&
            fixture.ViewModel.Items()[1].DisplayName == "Tree",
        "Full Forge Library must expose name-sorted Project Stamps.");
    Require(
        fixture.ViewModel.ThumbnailBuildCount() == 2U &&
            fixture.ViewModel.ThumbnailFor(Core::UUID{1U}) != nullptr &&
            fixture.ViewModel.ThumbnailFor(Core::UUID{2U}) != nullptr &&
            fixture.ViewModel.ThumbnailFor(Core::UUID{1U})
                    ->SourceVoxelCount == 3U &&
            fixture.ViewModel.ThumbnailFor(Core::UUID{1U})
                    ->Points.size() == 3U,
        "Refresh must cache one authoritative thumbnail per Stamp.");
    const std::size_t readsAfterThumbnails = fixture.Repository.ReadCount;
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.ThumbnailBuildCount() == 2U &&
            fixture.Repository.ReadCount == readsAfterThumbnails,
        "An unchanged refresh must reuse cached thumbnails without source reads.");
}

void TestSearchSortFiltersAndModes()
{
    Fixture fixture;
    fixture.Repository.Add(10U, "StoneArch.vfstamp", 3U, 40U);
    fixture.Repository.Add(20U, "Tree.vfstamp", 2U, 50U);
    Require(fixture.ViewModel.Refresh().Succeeded,
        "Initial Forge Library refresh must rebuild a missing catalogue.");

    fixture.ViewModel.SetSearchText("stone");
    Require(fixture.ViewModel.NeedsRefresh() &&
            fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U &&
            fixture.ViewModel.Items()[0].DisplayName == "StoneArch",
        "Search must filter Project Library names in real time.");

    fixture.ViewModel.SetSearchText("no-result");
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().empty() &&
            fixture.ViewModel.EmptyState() ==
                ForgeLibraryEmptyState::NoSearchResults,
        "A search miss must be distinct from an empty Project Library.");

    fixture.ViewModel.SetSearchText({});
    fixture.ViewModel.SetSortMode(ForgeLibrarySortMode::Date);
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 2U,
        "Prepared Date sorting must retain all V1 entries.");
    fixture.ViewModel.SetSortMode(ForgeLibrarySortMode::Type);
    Require(fixture.ViewModel.Items()[0].DisplayName == "StoneArch",
        "Prepared Type sorting must remain deterministic.");

    fixture.ViewModel.SetSection(ForgeLibrarySection::Favorites);
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().empty() &&
            fixture.ViewModel.EmptyState() ==
                ForgeLibraryEmptyState::FavoritesEmpty,
        "Favorites with no favorited creation must expose an empty state.");
    fixture.ViewModel.SetSection(ForgeLibrarySection::Creations);
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 2U,
        "Project filter must expose the Project Library catalogue.");

    fixture.ViewModel.SetDisplayMode(ForgeLibraryDisplayMode::List);
    fixture.ViewModel.ResetForProjectChange();
    Require(fixture.ViewModel.DisplayMode() == ForgeLibraryDisplayMode::List &&
            fixture.ViewModel.Items().empty() &&
            fixture.ViewModel.NeedsRefresh(),
        "Grid/List preference must survive a project change while content resets.");
}

void TestSelectionRenameDeletionAndActivation()
{
    Fixture fixture;
    fixture.Repository.Add(30U, "Chair.vfstamp", 3U, 60U);
    fixture.Repository.Add(40U, "Table.vfstamp", 2U, 70U);
    Require(fixture.ViewModel.Refresh().Succeeded,
        "Selection fixture must refresh.");
    const Core::UUID chair{30U};
    const std::size_t thumbnailBuilds =
        fixture.ViewModel.ThumbnailBuildCount();
    Require(fixture.ViewModel.Select(chair) &&
            fixture.ViewModel.SelectedDetails() != nullptr &&
            fixture.ViewModel.SelectedDetails()->Name == "Chair" &&
            fixture.ViewModel.SelectedPreviewStamp() != nullptr &&
            fixture.ViewModel.SelectedThumbnail() != nullptr &&
            fixture.ViewModel.SelectedThumbnail() ==
                fixture.ViewModel.ThumbnailFor(chair) &&
            fixture.ViewModel.ThumbnailBuildCount() == thumbnailBuilds,
        "Selection must load authoritative details and preview source.");

    auto document = MakeDocument();
    const ForgeLibraryOperationResult activated =
        fixture.ViewModel.ActivateSelected(document, 7U);
    Require(activated.Succeeded && activated.SessionActivated &&
            fixture.Session.IsActive() &&
            fixture.Session.CurrentPreview() != nullptr &&
            fixture.Session.ActiveStamp()->Identity().Id == chair,
        "Use must activate the existing placement session and preview.");

    fixture.Repository.Sources[0].Reference.RelativePath =
        "Assets/ForgeLibrary/Creations/RenamedChair.vfstamp";
    Require(fixture.Catalog.RebuildCatalogue().Succeeded() &&
            fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.SelectedId() != nullptr &&
            *fixture.ViewModel.SelectedId() == chair,
        "A renamed source must preserve UUID selection.");
    Require(fixture.ViewModel.Select(chair) &&
            fixture.ViewModel.SelectedDetails()->Name == "RenamedChair",
        "Renamed details must update from the rebuilt catalogue.");

    fixture.Repository.Sources.erase(fixture.Repository.Sources.begin());
    Require(fixture.Catalog.RebuildCatalogue().Succeeded() &&
            fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.SelectedId() == nullptr,
        "Deleting a selected source must clear stale selection safely.");
}

void TestOfficialNavigationScopesFavoritesAndRecent()
{
    Require(
        ForgeLibraryNavigationSections[0] == ForgeLibrarySection::Assets &&
            ForgeLibraryNavigationSections[1] ==
                ForgeLibrarySection::Creations &&
            ForgeLibraryNavigationSections[2] ==
                ForgeLibrarySection::Brushes &&
            ForgeLibraryNavigationSections[3] ==
                ForgeLibrarySection::Favorites &&
            ForgeLibraryNavigationSections[4] ==
                ForgeLibrarySection::Recent,
        "Forge Library navigation must keep Assets, Creations, Brushes, Favorites and Recent distinct.");

    Fixture fixture;
    fixture.Repository.Add(
        100U, "Oak.vfstamp", 3U, 80U, "Nature/Trees");
    fixture.Repository.Add(
        200U, "Arch.vfstamp", 2U, 90U, "Architecture/Doors");
    fixture.UserRepository.Add(
        100U, "PersonalRock.vfstamp", 4U, 100U, "Nature/Rocks",
        StampLibraryScope::User);

    Require(
        fixture.ViewModel.Scope() == ForgeLibraryScope::Project &&
            fixture.ViewModel.Section() == ForgeLibrarySection::Creations &&
            fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 2U &&
            fixture.ViewModel.Categories().size() == 2U,
        "Project Creations must be the default Forge Library content view.");

    fixture.ViewModel.SetSearchText("Nature");
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U &&
            fixture.ViewModel.Items().front().DisplayName == "Oak",
        "Search must match category/path tokens as well as creation names.");
    fixture.ViewModel.SetSearchText({});
    fixture.ViewModel.SetCategory("Architecture/Doors");
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U &&
            fixture.ViewModel.Items().front().DisplayName == "Arch",
        "Custom folder categories must filter creations without a compiled enum.");

    fixture.ViewModel.SetCategory({});
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Select(Core::UUID{100U}) &&
            fixture.ViewModel.ToggleFavorite(Core::UUID{100U}) &&
            fixture.ViewModel.IsFavorite(Core::UUID{100U}),
        "A Project creation must be selectable and favoritable.");
    fixture.ViewModel.SetSection(ForgeLibrarySection::Favorites);
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U &&
            fixture.ViewModel.Items().front().DisplayName == "Oak",
        "Favorites must expose only favorited creations.");

    fixture.ViewModel.SetSection(ForgeLibrarySection::Creations);
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Select(Core::UUID{100U}),
        "Favorite fixture must return to Creations.");
    auto document = MakeDocument();
    Require(
        fixture.ViewModel.ActivateSelected(document, 18U).SessionActivated,
        "Activating a creation must begin its exact placement session.");
    fixture.ViewModel.SetSection(ForgeLibrarySection::Recent);
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U &&
            fixture.ViewModel.Items().front().DisplayName == "Oak",
        "Recent must contain creations used for placement, newest first.");

    fixture.ViewModel.SetSection(ForgeLibrarySection::Brushes);
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().empty() &&
            fixture.ViewModel.EmptyState() ==
                ForgeLibraryEmptyState::BrushesEmpty,
        "Brushes must remain distinct and must never contain Stamp creations.");

    fixture.ViewModel.SetScope(ForgeLibraryScope::My);
    fixture.ViewModel.SetSection(ForgeLibrarySection::Creations);
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U &&
            fixture.ViewModel.Items().front().DisplayName == "PersonalRock" &&
            fixture.ViewModel.Items().front().CatalogEntry.Reference.Scope ==
                StampLibraryScope::User &&
            !fixture.ViewModel.IsFavorite(Core::UUID{100U}),
        "My Library must use its own scope, catalogue and favorite identity even when UUIDs overlap.");
    fixture.ViewModel.SetSearchText("Rocks");
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 1U,
        "My Library search must include custom category tokens.");
}

void TestMissingSourcePresentation()
{
    Fixture fixture;
    fixture.Repository.Add(300U, "MissingSoon.vfstamp", 2U, 120U);
    Require(fixture.ViewModel.Refresh().Succeeded,
        "Missing-source fixture must refresh before removal.");
    fixture.Repository.Sources.clear();
    Require(
        !fixture.ViewModel.Select(Core::UUID{300U}) &&
            fixture.ViewModel.SelectedId() != nullptr &&
            fixture.ViewModel.SelectedDetails() != nullptr &&
            fixture.ViewModel.SelectedDetails()->Name == "MissingSoon" &&
            !fixture.ViewModel.SelectedDetails()->SourceAvailable &&
            fixture.ViewModel.SelectedPreviewStamp() == nullptr &&
            !fixture.ViewModel.StatusMessage().empty(),
        "A missing source must retain an identifiable card selection and expose an unavailable preview.");
}

void TestResponsiveLayoutAndLongNames()
{
    Require(
        ResolveForgeLibraryResponsiveLayout(
            219.0F, ForgeLibraryDisplayMode::Grid, false).Mode ==
            ForgeLibraryResponsiveMode::CompactList,
        "Panels below 220 pixels must use compact List.");

    const ForgeLibraryResponsiveLayout oneColumn =
        ResolveForgeLibraryResponsiveLayout(
            250.0F, ForgeLibraryDisplayMode::Grid, true);
    Require(
        oneColumn.Mode == ForgeLibraryResponsiveMode::Grid &&
            oneColumn.GridColumns == 1U &&
            oneColumn.UseButtonVisible &&
            oneColumn.UseButtonFullWidth,
        "220-320 pixel Grid must reserve one column and a full-width Use button.");

    Require(
        ResolveForgeLibraryResponsiveLayout(
            400.0F, ForgeLibraryDisplayMode::Grid, false).GridColumns == 2U,
        "320-480 pixel Grid must expose two columns.");
    Require(
        ResolveForgeLibraryResponsiveLayout(
            640.0F, ForgeLibraryDisplayMode::Grid, false).GridColumns >= 3U,
        "Wide Grid must expose multiple columns.");
    Require(
        ResolveForgeLibraryResponsiveLayout(
            640.0F, ForgeLibraryDisplayMode::List, false).Mode ==
            ForgeLibraryResponsiveMode::List,
        "Explicit List mode must survive wide layouts.");

    Fixture fixture;
    const std::string longName =
        "Extremely Long Architectural Castle Gate Stamp Name.vfstamp";
    fixture.Repository.Add(90U, longName, 4U, 90U);
    Require(
        fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().front().DisplayName ==
                "Extremely Long Architectural Castle Gate Stamp Name",
        "The model must preserve the complete name for card tooltips.");
}

} // namespace

int main()
{
    try
    {
        TestEmptyAndCompleteViews();
        TestSearchSortFiltersAndModes();
        TestSelectionRenameDeletionAndActivation();
        TestOfficialNavigationScopesFavoritesAndRecent();
        TestMissingSourcePresentation();
        TestResponsiveLayoutAndLongNames();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
