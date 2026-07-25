#include "Selection/SelectionService.h"
#include "VoxelStamps/Library/StampCatalogService.h"
#include "VoxelStamps/Library/StampJsonCatalogStore.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Workflow/SaveSelectionAsStampWorkflow.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;
namespace fs = std::filesystem;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryProject final
{
public:
    TemporaryProject()
    {
        root_ = fs::temp_directory_path() / ("VoxelForgeSaveSelectionAsStamp-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(fs::create_directories(root_ / "Assets"), "Unable to create temporary project Assets.");
    }
    ~TemporaryProject()
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    [[nodiscard]] const fs::path& Root() const noexcept { return root_; }
private:
    fs::path root_;
};

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Models.push_back({
        .Dimensions = {.X = 8U, .Y = 8U, .Z = 8U},
        .Voxels = {{.X = 2U, .Y = 2U, .Z = 2U, .ColorIndex = 4U},
                   {.X = 3U, .Y = 2U, .Z = 2U, .ColorIndex = 7U}}});
    source.Palette[4U] = {.Red = 11U, .Green = 22U, .Blue = 33U, .Alpha = 255U};
    source.Palette[7U] = {.Red = 44U, .Green = 55U, .Blue = 66U, .Alpha = 255U};
    const auto built = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, fs::path{"save-selection-as-stamp.vox"});
    Require(built.Succeeded(), "Workflow fixture document must build.");
    return std::move(*built.Document);
}

struct Fixture final
{
    TemporaryProject Project;
    Asset::Voxel::VoxelDocument Document = MakeDocument();
    SelectionService Selection;
    StampProjectLibraryRepository Repository;
    StampJsonCatalogStore Store;
    SaveSelectionAsStampWorkflow Workflow{Repository, Store};

    Fixture()
    {
        Require(Repository.SetProjectRoot(Project.Root()), "Repository root must configure.");
        Require(Store.SetProjectRoot(Project.Root()), "Catalogue store root must configure.");
        Selection.SetDocumentGeneration(17U);
        const std::vector<Asset::Voxel::VoxelPosition> positions{{2, 2, 2}, {3, 2, 2}};
        Require(Selection.Apply(positions, SelectionMode::Replace),
            "Fixture selection must apply.");
    }

    [[nodiscard]] SaveSelectionAsStampBeginRequest BeginRequest() const
    {
        return {.ProjectRoot = Project.Root(), .Document = &Document, .Selection = &Selection,
                .DocumentGeneration = 17U, .DocumentRevision = Document.GetRevision()};
    }
    [[nodiscard]] SaveSelectionAsStampCurrentContext Current() const
    {
        return {.ProjectRoot = Project.Root(), .Document = &Document, .Selection = &Selection,
                .DocumentGeneration = 17U, .DocumentRevision = Document.GetRevision()};
    }
};

class FailingCatalogStore final : public IStampCatalogStore
{
public:
    bool FailWrites = true;
    StampJsonCatalogStore Backing;

    [[nodiscard]] bool Configure(const fs::path& root) { return Backing.SetProjectRoot(root); }
    [[nodiscard]] StampCatalogResult LoadCatalogue() const override
    {
        return Backing.LoadCatalogue();
    }
    [[nodiscard]] StampCatalogResult WriteCatalogueAtomically(const StampCatalog& catalogue) override
    {
        if (FailWrites)
            return {.Error = StampCatalogError::IoFailure,
                    .Message = "Injected catalogue write failure."};
        return Backing.WriteCatalogueAtomically(catalogue);
    }
};

void TestLifecycleNameValidationAndImmutableSnapshot()
{
    Fixture fixture;
    const std::uint64_t revisionBefore = fixture.Document.GetRevision();
    const std::vector<Asset::Voxel::VoxelPosition> selectionBefore(
        fixture.Selection.Voxels().begin(), fixture.Selection.Voxels().end());
    const auto begin = fixture.Workflow.Begin(fixture.BeginRequest());
    Require(begin.Status == SaveSelectionAsStampStatus::Ready && fixture.Workflow.Active(),
        "Begin must immediately capture a valid immutable selection snapshot.");
    Require(fixture.Workflow.Begin(fixture.BeginRequest()).Status == SaveSelectionAsStampStatus::Refused,
        "A second Begin must be refused while the dialog workflow is active.");
    for (const std::string invalid : {"", "  ", " stamp", "stamp ", "CON", "PRN.txt", "bad/name"})
        Require(fixture.Workflow.ValidateDraft({.Name = invalid}).Status == SaveSelectionAsStampStatus::Refused,
            "Invalid Windows and whitespace Stamp names must be refused without normalization.");
    const std::string utf8Name{"rocher-\xC3\xA9"};
    Require(fixture.Workflow.ValidateDraft({.Name = utf8Name}).Status == SaveSelectionAsStampStatus::Ready,
        "Valid UTF-8 Stamp names must be accepted without transliteration.");
    Require(fixture.Workflow.Cancel().Status == SaveSelectionAsStampStatus::Cancelled &&
                !fixture.Workflow.Active(),
        "Cancel must report cancellation and discard only the immutable workflow snapshot.");
    Require(fixture.Document.GetRevision() == revisionBefore &&
                std::vector<Asset::Voxel::VoxelPosition>(fixture.Selection.Voxels().begin(), fixture.Selection.Voxels().end()) == selectionBefore,
        "Opening and cancelling Save Selection As must not mutate the document or selection.");
}

void TestSoftLimitRequiresExplicitSaveAnywayConfirmation()
{
    Fixture fixture;
    auto request = fixture.BeginRequest();
    request.Limits.SoftVoxelCount = 1U;
    request.Limits.HardVoxelCount = 8U;
    const auto begin = fixture.Workflow.Begin(request);
    Require(begin.Status == SaveSelectionAsStampStatus::Ready && begin.RequiresSoftLimitConfirmation &&
                fixture.Workflow.RequiresSoftLimitConfirmation(),
        "A soft resource limit must retain the valid snapshot and request explicit confirmation.");
    Require(fixture.Workflow.ValidateDraft({.Name = "soft-limit"}).Status == SaveSelectionAsStampStatus::Refused &&
                fixture.Workflow.Save({.Name = "soft-limit"}, fixture.Current()).Status ==
                    SaveSelectionAsStampStatus::Refused,
        "Save must refuse a soft-limit selection until the user chooses Save Anyway.");
    Require(fixture.Workflow.Save({.Name = "soft-limit", .ConfirmSoftLimit = true}, fixture.Current()).IsSuccess(),
        "Save Anyway must install a soft-limit selection after explicit confirmation.");
}

void TestBeginRejectionsAndCaptureFailureDoNotWriteFiles()
{
    Fixture fixture;
    auto request = fixture.BeginRequest();
    request.ProjectRoot.clear();
    Require(fixture.Workflow.Begin(request).Status == SaveSelectionAsStampStatus::Refused,
        "Begin must refuse a missing project before capture.");
    request = fixture.BeginRequest();
    request.Document = nullptr;
    Require(fixture.Workflow.Begin(request).Status == SaveSelectionAsStampStatus::Refused,
        "Begin must refuse a missing document before capture.");
    SelectionService emptySelection;
    emptySelection.SetDocumentGeneration(17U);
    request = fixture.BeginRequest();
    request.Selection = &emptySelection;
    Require(fixture.Workflow.Begin(request).Status == SaveSelectionAsStampStatus::Refused,
        "Begin must refuse an empty selection before capture.");
    request = fixture.BeginRequest();
    request.Limits.HardVoxelCount = 1U;
    request.Limits.SoftVoxelCount = 1U;
    Require(fixture.Workflow.Begin(request).Status == SaveSelectionAsStampStatus::CaptureFailed &&
                !fs::exists(fixture.Project.Root() / "Assets/ForgeLibrary"),
        "A hard capture limit must fail before any Project Library file or directory is created.");
}

void TestSaveRevalidatesEveryLiveContextFact()
{
    Fixture fixture;
    Require(fixture.Workflow.Begin(fixture.BeginRequest()).Status == SaveSelectionAsStampStatus::Ready,
        "Context fixture must begin.");
    auto current = fixture.Current();
    current.ProjectRoot /= "different-project";
    Require(fixture.Workflow.Save({.Name = "context"}, current).Status == SaveSelectionAsStampStatus::Refused,
        "Save must refuse a changed project root.");
    Asset::Voxel::VoxelDocument otherDocument = MakeDocument();
    current = fixture.Current();
    current.Document = &otherDocument;
    Require(fixture.Workflow.Save({.Name = "context"}, current).Status == SaveSelectionAsStampStatus::Refused,
        "Save must refuse a changed document identity.");
    static_cast<void>(fixture.Document.ReplaceVoxelColor({2, 2, 2}, 7U));
    current = fixture.Current();
    Require(fixture.Workflow.Save({.Name = "context"}, current).Status == SaveSelectionAsStampStatus::Refused,
        "Save must refuse a real document revision change after capture.");
    static_cast<void>(fixture.Workflow.Cancel());

    Fixture selectionFixture;
    Require(selectionFixture.Workflow.Begin(selectionFixture.BeginRequest()).Status == SaveSelectionAsStampStatus::Ready,
        "Selection-change fixture must begin.");
    Require(selectionFixture.Selection.Select({2, 2, 2}), "Selection must be changeable after capture.");
    Require(selectionFixture.Workflow.Save({.Name = "context"}, selectionFixture.Current()).Status ==
                SaveSelectionAsStampStatus::Refused,
        "Save must refuse a changed selection after capture.");
}

void TestInstallFailureAndDeterministicCollisionNames()
{
    TemporaryProject project;
    Asset::Voxel::VoxelDocument document = MakeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(4U);
    Require(selection.Select({2, 2, 2}), "Install-failure fixture selection must apply.");
    StampProjectLibraryRepository unconfiguredRepository;
    StampJsonCatalogStore store;
    SaveSelectionAsStampWorkflow installFailure(unconfiguredRepository, store);
    const SaveSelectionAsStampBeginRequest request{
        .ProjectRoot = project.Root(), .Document = &document, .Selection = &selection,
        .DocumentGeneration = 4U, .DocumentRevision = document.GetRevision()};
    const SaveSelectionAsStampCurrentContext current{
        .ProjectRoot = project.Root(), .Document = &document, .Selection = &selection,
        .DocumentGeneration = 4U, .DocumentRevision = document.GetRevision()};
    Require(installFailure.Begin(request).Status == SaveSelectionAsStampStatus::Ready &&
                installFailure.Save({.Name = "no-repository"}, current).Status ==
                    SaveSelectionAsStampStatus::InstallFailed && installFailure.Active(),
        "An installation failure must not mutate the document and must retain the snapshot for retry or cancellation.");

    Fixture fixture;
    Require(fixture.Workflow.Begin(fixture.BeginRequest()).Status == SaveSelectionAsStampStatus::Ready,
        "First collision fixture must begin.");
    const auto first = fixture.Workflow.Save({.Name = "same-name"}, fixture.Current());
    Require(first.IsSuccess(), "First collision fixture save must succeed.");
    Require(fixture.Workflow.Begin(fixture.BeginRequest()).Status == SaveSelectionAsStampStatus::Ready,
        "Second collision fixture must begin.");
    const auto second = fixture.Workflow.Save({.Name = "same-name"}, fixture.Current());
    Require(second.IsSuccess() && second.InstalledAsset &&
                second.InstalledAsset->RelativePath.filename() == "same-name-2.vfstamp",
        "Existing Stamp names must receive the deterministic -2 collision suffix.");
}

void TestContextRevalidationAndSuccessCatalogueRecovery()
{
    Fixture fixture;
    Require(fixture.Workflow.Begin(fixture.BeginRequest()).Status == SaveSelectionAsStampStatus::Ready,
        "Valid workflow must begin.");
    auto changed = fixture.Current();
    ++changed.DocumentGeneration;
    Require(fixture.Workflow.Save({.Name = "snapshot"}, changed).Status == SaveSelectionAsStampStatus::Refused &&
                fixture.Workflow.Active(),
        "Save must revalidate project/document generation/revision/selection without discarding a failed snapshot.");
    const std::string utf8Name{"rocher-\xC3\xA9"};
    const auto saved = fixture.Workflow.Save({.Name = utf8Name}, fixture.Current());
    const std::string expectedUtf8Bytes = utf8Name + ".vfstamp";
    const std::u8string expectedUtf8Name(
        reinterpret_cast<const char8_t*>(expectedUtf8Bytes.data()),
        reinterpret_cast<const char8_t*>(expectedUtf8Bytes.data()) + expectedUtf8Bytes.size());
    Require(saved.Status == SaveSelectionAsStampStatus::CompleteSuccess && saved.InstalledAsset &&
                saved.InstalledAsset->RelativePath.filename().u8string() == expectedUtf8Name &&
                !fixture.Workflow.Active(),
        "Save must install an exact UTF-8 name and rebuild the derived Project Library catalogue.");
    Require(fixture.Repository.Read(*saved.InstalledAsset).Succeeded() &&
                fixture.Store.LoadCatalogue().Succeeded(),
        "Successful save must leave a coherent derived catalogue.");

    TemporaryProject project;
    Asset::Voxel::VoxelDocument document = MakeDocument();
    SelectionService selection;
    selection.SetDocumentGeneration(5U);
    Require(selection.Select({2, 2, 2}), "Failure fixture selection must apply.");
    StampProjectLibraryRepository repository;
    FailingCatalogStore store;
    Require(repository.SetProjectRoot(project.Root()) && store.Configure(project.Root()),
        "Partial-success fixture services must configure.");
    SaveSelectionAsStampWorkflow workflow(repository, store);
    const SaveSelectionAsStampBeginRequest request{
        .ProjectRoot = project.Root(), .Document = &document, .Selection = &selection,
        .DocumentGeneration = 5U, .DocumentRevision = document.GetRevision()};
    Require(workflow.Begin(request).Status == SaveSelectionAsStampStatus::Ready,
        "Partial-success workflow must capture.");
    const SaveSelectionAsStampCurrentContext current{
        .ProjectRoot = project.Root(), .Document = &document, .Selection = &selection,
        .DocumentGeneration = 5U, .DocumentRevision = document.GetRevision()};
    const auto partial = workflow.Save({.Name = "catalogue-recovery"}, current);
    Require(partial.Status == SaveSelectionAsStampStatus::PartialSuccess && partial.InstalledAsset,
        "Catalogue publication failure after install must report partial success without losing the asset.");
    store.FailWrites = false;
    StampCatalogService recovery(repository, store);
    Require(recovery.RebuildCatalogue().Succeeded(),
        "A later catalogue rebuild must recover from a failed derived-catalogue write.");
}

} // namespace

int main()
{
    try
    {
        TestLifecycleNameValidationAndImmutableSnapshot();
        TestBeginRejectionsAndCaptureFailureDoNotWriteFiles();
        TestSoftLimitRequiresExplicitSaveAnywayConfirmation();
        TestSaveRevalidatesEveryLiveContextFact();
        TestInstallFailureAndDeterministicCollisionNames();
        TestContextRevalidationAndSuccessCatalogueRecovery();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
