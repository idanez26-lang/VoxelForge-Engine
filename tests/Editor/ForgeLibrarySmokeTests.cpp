#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Library/ForgeLibraryViewModel.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] VoxelStamp MakeStamp()
{
    StampValidationResult validation{};
    auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{0x464f5247453138ULL},
         .ContentHash = "forge-library-smoke"},
        {{}, {1, 0, 0}, {2U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Corner,
         .ResolvedMode = StampPivotMode::Corner,
         .LocalPosition = {}},
        {},
        {{0U, {10U, 120U, 220U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value(), validation.Message);
    return std::move(*stamp);
}

class SmokeRepository final : public IStampLibraryRepository
{
public:
    [[nodiscard]] StampLibraryResult Install(
        const VoxelStamp& stamp, const StampInstallOptions&) override
    {
        Stamp = stamp;
        Reference = {
            .Id = stamp.Identity().Id,
            .ContentHash = stamp.Identity().ContentHash,
            .RelativePath =
                "Assets/ForgeLibrary/Creations/SmokeStamp.vfstamp"};
        return {.Reference = Reference, .Stamp = Stamp};
    }
    [[nodiscard]] StampLibraryResult Read(
        const StampAssetReference& reference) const override
    {
        if (!Stamp || reference != Reference)
            return {.Error = StampLibraryError::AssetNotFound};
        return {.Reference = Reference, .Stamp = *Stamp};
    }
    [[nodiscard]] StampLibraryResult EnumerateSourceAssets() const override
    {
        return Stamp
            ? StampLibraryResult{.Assets = {{Reference, 256U}}}
            : StampLibraryResult{};
    }
    [[nodiscard]] StampLibraryResult Remove(
        const StampAssetReference&) override
    {
        return {.Error = StampLibraryError::InvalidReference};
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

    std::optional<VoxelStamp> Stamp;
    StampAssetReference Reference;
};

class SmokeStore final : public IStampCatalogStore
{
public:
    [[nodiscard]] StampCatalogResult LoadCatalogue() const override
    {
        return Catalog
            ? StampCatalogResult{.Catalog = *Catalog}
            : StampCatalogResult{.Error = StampCatalogError::Missing};
    }
    [[nodiscard]] StampCatalogResult WriteCatalogueAtomically(
        const StampCatalog& catalogue) override
    {
        Catalog = catalogue;
        return {.Catalog = catalogue};
    }
    std::optional<StampCatalog> Catalog;
};

[[nodiscard]] Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {12U, 6U, 12U}});
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "forge-library-smoke.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Forge Library smoke document must build.");
    return std::move(*loaded.Document);
}

[[nodiscard]] Voxel::VoxelModel MakeCompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    for (std::size_t index = 0U; index < document.GetPalette().size(); ++index)
    {
        const auto color = document.GetPalette()[index];
        Require(model.Palette().Set(
                    index, {color.Red, color.Green, color.Blue, color.Alpha}),
            "Forge Library smoke palette must initialize.");
    }
    const auto* source = document.GetModel(0U);
    Require(source != nullptr, "Forge Library smoke requires one model.");
    Voxel::VoxelGrid grid;
    const auto dimensions = source->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Forge Library smoke grid must initialize.");
    model.AddGrid(std::move(grid));
    return model;
}

class SmokeEditSession final : public VoxelEditSession
{
public:
    explicit SmokeEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibilityModel(document))
    {
    }
    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 18U;
    }
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }
    [[nodiscard]] Asset::Voxel::VoxelDocument*
    ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    {
        const auto synchronized = cache_.Synchronize(*document_, 18U);
        return synchronized.Succeeded
            ? CommandResult::Success()
            : CommandResult::Failure(synchronized.Message);
    }
    void CompleteVoxelEdit() noexcept override {}

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    Mesh::VoxelDocumentMeshCache cache_;
};

void RunSmoke()
{
    SmokeRepository repository;
    SmokeStore store;
    StampCatalogService catalog(repository, store);
    StampPlacementSession placement;
    ForgeLibraryViewModel library(catalog, repository, placement);
    auto document = MakeDocument();
    SmokeEditSession editSession(document);
    VoxelEditHistory history;

    Require(
        library.Refresh().Succeeded &&
            library.Items().empty() &&
            library.EmptyState() == ForgeLibraryEmptyState::EmptyProject,
        "An empty project must open an actionable Forge Library state.");
    VoxelStamp created = MakeStamp();
    Require(
        repository.Install(created, {}).Succeeded(),
        "Save Selection As Stamp equivalent must publish the smoke Stamp.");
    catalog.InvalidateCache();
    Require(
        catalog.RebuildCatalogue().Succeeded() &&
            library.Refresh().Succeeded &&
            library.Items().size() == 1U,
        "The newly created Stamp must appear in Forge Library.");
    Require(library.Select(repository.Reference.Id),
        "Forge Library item selection must load.");
    Require(library.ActivateSelected(document, 18U).SessionActivated &&
            placement.CurrentPreview() != nullptr,
        "Use must activate the shared placement preview.");

    auto first = PreparePlaceVoxelStampOperation(*placement.CurrentPlan());
    Require(first.IsReady() &&
            history.Execute(editSession, std::move(first.Operation)),
        "First Forge Library placement must commit.");
    placement.MarkPlacementCommitted();
    Require(placement.TranslateTarget(3, 0, 0, document, 18U).Succeeded,
        "Persistent preview must move for a second placement.");
    auto second = PreparePlaceVoxelStampOperation(*placement.CurrentPlan());
    Require(second.IsReady() &&
            history.Execute(editSession, std::move(second.Operation)),
        "Second Forge Library placement must commit.");
    placement.MarkPlacementCommitted();
    Require(history.UndoCount() == 2U &&
            static_cast<bool>(history.Undo(editSession)) &&
            static_cast<bool>(history.Redo(editSession)),
        "Forge Library smoke Undo/Redo must remain atomic.");
    Require(placement.Rebuild(document, 18U).Succeeded &&
            placement.CurrentPreview() != nullptr,
        "Preview must persist after placement and history changes.");
    Require(placement.Cancel() && !placement.IsActive(),
        "Close/Esc must cancel the Forge Library placement session.");
}

} // namespace

int main()
{
    try
    {
        RunSmoke();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
