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
        const std::uint8_t red)
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
                    std::filesystem::path("Assets/ForgeLibrary/Creations") /
                    name},
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
    MemoryStore Store;
    StampPlacementSession Session;
    StampCatalogService Catalog{Repository, Store};
    ForgeLibraryViewModel ViewModel{Catalog, Repository, Session};
};

void TestEmptyAndCompleteViews()
{
    Fixture fixture;
    const auto empty = fixture.ViewModel.Refresh();
    Require(empty.Succeeded && fixture.ViewModel.Items().empty(),
        "An empty Project Library must be a successful empty view.");

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

    fixture.ViewModel.SetSearchText({});
    fixture.ViewModel.SetSortMode(ForgeLibrarySortMode::Date);
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().size() == 2U,
        "Prepared Date sorting must retain all V1 entries.");
    fixture.ViewModel.SetSortMode(ForgeLibrarySortMode::Type);
    Require(fixture.ViewModel.Items()[0].DisplayName == "StoneArch",
        "Prepared Type sorting must remain deterministic.");

    fixture.ViewModel.SetFilter(ForgeLibraryFilter::Favorites);
    Require(fixture.ViewModel.Refresh().Succeeded &&
            fixture.ViewModel.Items().empty(),
        "Favorites must be an intentionally empty prepared V1 view.");
    fixture.ViewModel.SetFilter(ForgeLibraryFilter::Project);
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
    Require(fixture.ViewModel.Select(chair) &&
            fixture.ViewModel.SelectedDetails() != nullptr &&
            fixture.ViewModel.SelectedDetails()->Name == "Chair" &&
            fixture.ViewModel.SelectedPreviewStamp() != nullptr,
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

} // namespace

int main()
{
    try
    {
        TestEmptyAndCompleteViews();
        TestSearchSortFiltersAndModes();
        TestSelectionRenameDeletionAndActivation();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
