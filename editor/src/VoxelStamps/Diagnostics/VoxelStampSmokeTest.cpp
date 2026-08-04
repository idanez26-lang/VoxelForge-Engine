#include "VoxelStamps/Diagnostics/VoxelStampSmokeTest.h"

#include "Commands/Voxel/VoxelEditSession.h"
#include "Selection/SelectionService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Library/ForgeLibraryViewModel.h"
#include "VoxelStamps/Library/StampJsonCatalogStore.h"
#include "VoxelStamps/Library/StampLibraryPaths.h"
#include "VoxelStamps/Library/StampMemoryCatalogStore.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Library/StampUserLibraryRepository.h"
#include "VoxelStamps/SmartPlacement/StampSmartPlacementService.h"
#include "VoxelStamps/Variants/StampVariantGroup.h"
#include "VoxelStamps/Workflow/SaveSelectionAsStampWorkflow.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Core/UserDataPaths.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoxelForge::Editor::Stamps
{
namespace
{

namespace fs = std::filesystem;
constexpr std::int32_t Fixed = StampFixedPoint::UnitsPerVoxel;
constexpr std::uint64_t SmokeDocumentGeneration = 23U;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class IsolatedSmokeFixture final
{
public:
    explicit IsolatedSmokeFixture(const std::string_view label)
    {
        root_ = fs::temp_directory_path() /
            ("VoxelForgeStamp23-" + std::string(label) + "-" +
             std::to_string(std::chrono::steady_clock::now()
                 .time_since_epoch().count()));
        projectRoot_ = root_ / "Project";
        relocatedProjectRoot_ = root_ / "RelocatedProject";
        localAppDataRoot_ = root_ / "LocalAppData";
        std::error_code error;
        fs::create_directories(projectRoot_ / "Assets" / "Models", error);
        Require(!error, "Unable to create the isolated Stamp project.");
        fs::create_directories(
            Core::UserDataPaths({}, localAppDataRoot_).LocalDataDirectory(),
            error);
        Require(!error, "Unable to create the isolated Stamp profile.");
        root_ = fs::weakly_canonical(root_);
        projectRoot_ = root_ / "Project";
        relocatedProjectRoot_ = root_ / "RelocatedProject";
        localAppDataRoot_ = root_ / "LocalAppData";
    }

    ~IsolatedSmokeFixture()
    {
        if (cleaned_) return;
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    IsolatedSmokeFixture(const IsolatedSmokeFixture&) = delete;
    IsolatedSmokeFixture& operator=(const IsolatedSmokeFixture&) = delete;

    [[nodiscard]] const fs::path& Root() const noexcept { return root_; }
    [[nodiscard]] const fs::path& ProjectRoot() const noexcept
    {
        return projectRoot_;
    }
    [[nodiscard]] const fs::path& RelocatedProjectRoot() const noexcept
    {
        return relocatedProjectRoot_;
    }
    [[nodiscard]] const fs::path& LocalAppDataRoot() const noexcept
    {
        return localAppDataRoot_;
    }

    [[nodiscard]] bool Cleanup() noexcept
    {
        std::error_code error;
        fs::remove_all(root_, error);
        std::error_code existsError;
        cleaned_ = !fs::exists(root_, existsError) && !existsError;
        return cleaned_ && !error;
    }

private:
    fs::path root_;
    fs::path projectRoot_;
    fs::path relocatedProjectRoot_;
    fs::path localAppDataRoot_;
    bool cleaned_ = false;
};

template<typename Byte>
void WriteBytes(const fs::path& path, const std::vector<Byte>& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(stream), "Unable to create a Stamp smoke file.");
    if (!bytes.empty())
    {
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size() * sizeof(Byte)));
    }
    stream.flush();
    Require(static_cast<bool>(stream), "Unable to write a Stamp smoke file.");
}

void WriteText(const fs::path& path, const std::string_view text)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(stream), "Unable to create a Stamp smoke text file.");
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    Require(static_cast<bool>(stream), "Unable to write a Stamp smoke text file.");
}

[[nodiscard]] std::string ReadText(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    Require(static_cast<bool>(stream), "Unable to read a Stamp smoke text file.");
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

[[nodiscard]] Asset::Voxel::VoxelDocument MakeDocument(
    const fs::path& path,
    const bool withCaptureSource = false,
    const Asset::Vox::VoxDimensions dimensions = {24U, 8U, 8U})
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Palette[3U] = {210U, 65U, 45U, 255U};
    source.Palette[4U] = {55U, 170U, 95U, 255U};
    source.Palette[5U] = {55U, 105U, 220U, 255U};
    source.HasCustomPalette = true;
    Asset::Vox::VoxModelMetadata model{.Dimensions = dimensions};
    if (withCaptureSource)
    {
        model.Voxels = {
            {.X = 1U, .Y = 1U, .Z = 1U, .ColorIndex = 3U},
            {.X = 2U, .Y = 1U, .Z = 1U, .ColorIndex = 4U},
            {.X = 3U, .Y = 1U, .Z = 1U, .ColorIndex = 3U},
            {.X = 1U, .Y = 2U, .Z = 2U, .ColorIndex = 5U},
            {.X = 3U, .Y = 2U, .Z = 2U, .ColorIndex = 5U}};
    }
    source.Models.push_back(std::move(model));
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source, path);
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create the Stamp smoke document.");
    return std::move(*loaded.Document);
}

[[nodiscard]] VoxelStamp MakeSimpleStamp(
    const std::uint64_t id,
    const StampColor color,
    const StampPivotMode pivotMode = StampPivotMode::Corner)
{
    StampValidationResult validation{};
    auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = {}},
        {{}, {}, {1U, 1U, 1U}},
        {.RequestedMode = pivotMode,
         .ResolvedMode = pivotMode,
         .LocalPosition = {},
         .LocalNormal = pivotMode == StampPivotMode::Surface
            ? StampNormal{.Z = 1}
            : StampNormal{}},
        {}, {{0U, color}}, {{{0, 0, 0}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create a Stamp smoke asset.");
    return std::move(*stamp);
}

[[nodiscard]] VoxelStamp MakeSurfaceStamp()
{
    StampValidationResult validation{};
    auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{0x230501U},
         .ContentHash = "stamp-23-smart-placement"},
        {{}, {0, 0, 1}, {1U, 1U, 2U}},
        {.RequestedMode = StampPivotMode::Surface,
         .ResolvedMode = StampPivotMode::Surface,
         .LocalPosition = {},
         .LocalNormal = {.Z = 1},
         .AutoPolicyVersion = 1U},
        {}, {{0U, {45U, 125U, 220U, 255U}}},
        {{{0, 0, 0}, 0U}, {{0, 0, 1}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create the Smart Placement smoke asset.");
    return std::move(*stamp);
}

class SmokeEditSession final : public VoxelEditSession
{
public:
    explicit SmokeEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document)
    {
    }

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return SmokeDocumentGeneration;
    }
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return nullptr;
    }
    [[nodiscard]] Asset::Voxel::VoxelDocument*
    ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    {
        ++RebuildCount;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++CompletionCount; }

    std::size_t RebuildCount = 0U;
    std::size_t CompletionCount = 0U;

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
};

void RunMvpSmoke()
{
    IsolatedSmokeFixture fixture("mvp");
    const fs::path modelPath =
        fixture.ProjectRoot() / "Assets" / "Models" / "release-gate.vox";
    auto document = MakeDocument(modelPath, true);
    SelectionService selection;
    selection.SetDocumentGeneration(SmokeDocumentGeneration);
    for (const Asset::Voxel::VoxelPosition position : {
             Asset::Voxel::VoxelPosition{1, 1, 1}, {2, 1, 1}, {3, 1, 1},
             {1, 2, 2}, {3, 2, 2}})
    {
        Require(selection.Select(position),
            "Unable to build the recognizable Stamp smoke selection.");
    }

    StampProjectLibraryRepository repository;
    StampJsonCatalogStore store;
    Require(repository.SetProjectRoot(fixture.ProjectRoot()) &&
            store.SetProjectRoot(fixture.ProjectRoot()),
        "Unable to configure the MVP Project Library.");
    SaveSelectionAsStampWorkflow saveWorkflow(repository, store);
    const SaveSelectionAsStampBeginRequest begin{
        .ProjectRoot = fixture.ProjectRoot(),
        .Document = &document,
        .Selection = &selection,
        .DocumentGeneration = SmokeDocumentGeneration,
        .DocumentRevision = document.GetRevision()};
    Require(saveWorkflow.Begin(begin).Status ==
            SaveSelectionAsStampStatus::Ready,
        "MVP capture did not become ready.");
    const SaveSelectionAsStampCurrentContext current{
        .ProjectRoot = fixture.ProjectRoot(),
        .Document = &document,
        .Selection = &selection,
        .DocumentGeneration = SmokeDocumentGeneration,
        .DocumentRevision = document.GetRevision()};
    const SaveSelectionAsStampResult saved = saveWorkflow.Save(
        {.Name = "release-gate"}, current);
    Require(saved.Status == SaveSelectionAsStampStatus::CompleteSuccess &&
            saved.InstalledAsset,
        "Save Selection As did not publish the MVP Stamp and catalogue entry.");

    StampCatalogService catalogue(repository, store);
    const StampCatalogResult search = catalogue.Query({.Text = "release-gate"});
    Require(search.Succeeded() && search.Catalog.Entries.size() == 1U &&
            search.Catalog.Entries.front().Reference == *saved.InstalledAsset,
        "The saved Stamp was not discoverable through catalogue search.");
    StampAssetCache assetCache(repository);
    StampPlacementSession placement;
    ForgeLibraryViewModel library(catalogue, assetCache, placement);
    library.SetSearchText("release-gate");
    Require(library.Refresh().Succeeded && library.Items().size() == 1U &&
            library.Select(saved.InstalledAsset->Id),
        "Forge Library could not select the saved MVP Stamp.");
    Require(library.ActivateSelected(
                document, SmokeDocumentGeneration).SessionActivated,
        "Forge Library did not begin an exact live preview.");
    const StampFixedPoint capturedPivot =
        placement.ActiveStamp()->Pivot().LocalPosition;
    const StampPlacementSessionResult firstTarget = placement.SetTarget(
        {capturedPivot.X + 6 * Fixed,
         capturedPivot.Y + 2 * Fixed,
         capturedPivot.Z + 2 * Fixed},
        document, SmokeDocumentGeneration);
    if (!firstTarget.Succeeded || placement.CurrentPreview() == nullptr)
    {
        throw std::runtime_error(
            std::string("The MVP preview did not follow its first target: ") +
            std::string(StampPlacementDiagnosticMessage(
                firstTarget.Diagnostic)));
    }

    SmokeEditSession editSession(document);
    VoxelEditHistory history;
    const auto first = placement.PlaceOnce(
        document, SmokeDocumentGeneration, editSession, history);
    if (!static_cast<bool>(first) || history.UndoCount() != 1U ||
        !placement.IsActive() || placement.CurrentPreview() == nullptr)
    {
        throw std::runtime_error(
            "The first continuous Stamp placement failed (status=" +
            std::to_string(static_cast<unsigned int>(first.Status)) +
            ", history=" +
            std::to_string(static_cast<unsigned int>(first.History.Code)) +
            "): " + first.History.Message);
    }
    Require(placement.SetTarget(
                {capturedPivot.X + 12 * Fixed,
                 capturedPivot.Y + 2 * Fixed,
                 capturedPivot.Z + 2 * Fixed},
                document, SmokeDocumentGeneration).Succeeded,
        "The continuous preview did not move to its second target.");
    const auto second = placement.PlaceOnce(
        document, SmokeDocumentGeneration, editSession, history);
    Require(static_cast<bool>(second) && history.UndoCount() == 2U &&
            placement.PlacementOrdinal() == 2U,
        "The second continuous Stamp placement failed.");
    Require(placement.Cancel() && !placement.IsActive() &&
            placement.CurrentPreview() == nullptr,
        "Esc/cancel did not terminate the continuous placement session.");
    Require(history.Undo(editSession) && history.Undo(editSession) &&
            history.UndoCount() == 0U && history.RedoCount() == 2U,
        "The two MVP placements did not Undo atomically.");
    Require(history.Redo(editSession) && history.Redo(editSession) &&
            history.UndoCount() == 2U && history.RedoCount() == 0U,
        "The two MVP placements did not Redo atomically.");

    const Asset::Voxel::VoxDocumentWriteResult serialized =
        Asset::Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(serialized.Succeeded(), "The placed MVP document did not serialize.");
    WriteBytes(modelPath, serialized.Bytes);
    auto reopened = Asset::Voxel::VoxDocumentLoader{}.Load(modelPath);
    std::string difference;
    Require(reopened.Succeeded() && reopened.Document &&
            Asset::Voxel::AreVoxelDocumentsEquivalent(
                document, *reopened.Document, difference),
        "The saved MVP document did not reopen with exact voxels and palette.");

    StampProjectLibraryRepository restartRepository;
    StampJsonCatalogStore restartStore;
    Require(restartRepository.SetProjectRoot(fixture.ProjectRoot()) &&
            restartStore.SetProjectRoot(fixture.ProjectRoot()),
        "The restarted Project Library could not configure.");
    StampCatalogService restartCatalogue(restartRepository, restartStore);
    StampAssetCache restartCache(restartRepository);
    Require(restartCatalogue.Query({.Text = "release-gate"}).Succeeded() &&
            restartCache.GetOrLoad(*saved.InstalledAsset).Succeeded(),
        "The Stamp did not survive a fresh repository/catalogue/cache session.");

    std::error_code copyError;
    fs::copy(fixture.ProjectRoot(), fixture.RelocatedProjectRoot(),
        fs::copy_options::recursive, copyError);
    Require(!copyError, "The MVP project could not be relocated.");
    StampProjectLibraryRepository relocatedRepository;
    StampJsonCatalogStore relocatedStore;
    Require(relocatedRepository.SetProjectRoot(
                fixture.RelocatedProjectRoot()) &&
            relocatedStore.SetProjectRoot(fixture.RelocatedProjectRoot()) &&
            relocatedRepository.Read(*saved.InstalledAsset).Succeeded(),
        "The relocated Project Library did not resolve its portable Stamp reference.");
    StampCatalogService relocatedCatalogue(relocatedRepository, relocatedStore);
    Require(relocatedCatalogue.Query({.Text = "release-gate"}).Succeeded(),
        "The relocated Project Library catalogue did not reopen.");
    Require(ReadText(relocatedStore.Path()).find(
                fixture.ProjectRoot().generic_string()) == std::string::npos,
        "The Project Library catalogue leaked its original absolute root.");
    Require(fixture.Cleanup(),
        "The MVP smoke left a temporary project or profile artifact.");
}

void RunCorruptionSmoke()
{
    IsolatedSmokeFixture fixture("corruption");
    auto document = MakeDocument(
        fixture.ProjectRoot() / "Assets" / "Models" / "corruption.vox");
    const std::uint64_t originalRevision = document.GetRevision();
    StampProjectLibraryRepository repository;
    StampJsonCatalogStore store;
    Require(repository.SetProjectRoot(fixture.ProjectRoot()) &&
            store.SetProjectRoot(fixture.ProjectRoot()),
        "Unable to configure the corruption smoke library.");
    Require(repository.Install(
                MakeSimpleStamp(0x230201U, {170U, 90U, 45U, 255U}),
                {.PreferredFileStem = "valid"}).Succeeded(),
        "Unable to install the valid corruption-control Stamp.");
    StampCatalogService catalogue(repository, store);
    Require(catalogue.RebuildCatalogue().Succeeded(),
        "Unable to build the corruption-control catalogue.");

    const fs::path malformed = fixture.ProjectRoot() /
        ProjectCreationsRelativePath / "malformed.vfstamp";
    WriteText(malformed, "not a vfstamp");
    WriteText(store.Path(), "{corrupt catalogue");
    catalogue.InvalidateCache();
    Require(catalogue.Query({}).Error == StampCatalogError::Invalid,
        "A corrupt derived catalogue was not reported explicitly.");
    const StampCatalogResult rebuilt = catalogue.RebuildCatalogue();
    Require(rebuilt.Succeeded() && rebuilt.Catalog.Entries.size() == 1U &&
            std::any_of(rebuilt.Diagnostics.begin(), rebuilt.Diagnostics.end(),
                [&malformed](const StampCatalogDiagnostic& diagnostic)
                {
                    return diagnostic.RelativePath.filename() ==
                        malformed.filename();
                }),
        "Catalogue recovery did not retain valid sources and diagnose the malformed source.");
    Require(fs::exists(malformed) &&
            document.GetRevision() == originalRevision &&
            !document.GetVoxel({0, 0, 0}),
        "Corruption recovery deleted a source or mutated the active document.");
    Require(fixture.Cleanup(),
        "The corruption smoke left a temporary project or profile artifact.");
}

void RunLibrarySmoke()
{
    IsolatedSmokeFixture fixture("library");
    StampProjectLibraryRepository projectRepository;
    StampJsonCatalogStore projectStore;
    StampUserLibraryRepository userRepository(
        Core::UserDataPaths({}, fixture.LocalAppDataRoot()));
    StampMemoryCatalogStore userStore;
    Require(projectRepository.SetProjectRoot(fixture.ProjectRoot()),
        "Unable to configure the isolated Project library repository.");
    Require(projectStore.SetProjectRoot(fixture.ProjectRoot()),
        "Unable to configure the isolated Project catalogue store.");
    Require(!userRepository.UserDataRoot().empty(),
        "Unable to configure the isolated My Library repository.");
    const StampLibraryResult projectInstalled = projectRepository.Install(
        MakeSimpleStamp(0x230301U, {180U, 100U, 60U, 255U}),
        {.PreferredFileStem = "project-creation"});
    const StampLibraryResult userInstalled = userRepository.Install(
        MakeSimpleStamp(0x230302U, {60U, 150U, 210U, 255U}),
        {.PreferredFileStem = "personal-creation"});
    Require(projectInstalled.Succeeded() && userInstalled.Succeeded() &&
            projectInstalled.Reference.Scope == StampLibraryScope::Project &&
            userInstalled.Reference.Scope == StampLibraryScope::User &&
            !projectInstalled.Reference.RelativePath.is_absolute() &&
            !userInstalled.Reference.RelativePath.is_absolute(),
        "Project/My Library references were not scoped and portable.");

    StampCatalogService projectCatalogue(projectRepository, projectStore);
    StampCatalogService userCatalogue(userRepository, userStore);
    Require(projectCatalogue.RebuildCatalogue().Succeeded() &&
            userCatalogue.RebuildCatalogue().Succeeded(),
        "Unable to derive the isolated Project/My catalogues.");
    StampAssetCache cache(projectRepository, userRepository);
    StampPlacementSession placement;
    ForgeLibraryViewModel library(
        projectCatalogue, userCatalogue, cache, placement);
    Require(library.Refresh().Succeeded && library.Items().size() == 1U &&
            library.Items().front().CatalogEntry.Reference.Scope ==
                StampLibraryScope::Project,
        "Forge Library did not present the Project scope.");
    library.SetScope(ForgeLibraryScope::My);
    Require(library.Refresh().Succeeded && library.Items().size() == 1U &&
            library.Items().front().CatalogEntry.Reference.Scope ==
                StampLibraryScope::User,
        "Forge Library did not present the isolated My Library scope.");

    const std::string projectCatalogueText = ReadText(projectStore.Path());
    Require(projectCatalogueText.find(
                fixture.LocalAppDataRoot().generic_string()) ==
                std::string::npos &&
            projectCatalogueText.find(
                fixture.LocalAppDataRoot().string()) == std::string::npos,
        "A personal profile path leaked into the project catalogue.");

    library.SetScope(ForgeLibraryScope::Project);
    Require(library.Refresh().Succeeded &&
            library.Select(projectInstalled.Reference.Id),
        "Unable to select the missing-source presentation fixture.");
    Require(projectRepository.Remove(projectInstalled.Reference).Succeeded(),
        "Unable to remove the missing-source presentation fixture.");
    static_cast<void>(cache.Invalidate(projectInstalled.Reference));
    auto document = MakeDocument(
        fixture.ProjectRoot() / "Assets" / "Models" / "library.vox");
    const ForgeLibraryOperationResult missing = library.ActivateSelected(
        document, SmokeDocumentGeneration);
    Require(!missing.SessionActivated && !missing.Message.empty() &&
            !placement.IsActive() && library.Items().size() == 1U,
        "A missing source did not remain visible with an explicit safe failure.");
    Require(fixture.Cleanup(),
        "The library smoke left a temporary project or profile artifact.");
}

[[nodiscard]] StampVariantGroup MakeVariantGroup(
    const StampAssetReference& first,
    const StampAssetReference& second)
{
    StampVariantGroupValidation validation{};
    auto group = StampVariantGroup::TryCreate(
        {.Id = Core::UUID{0x230400U},
         .Name = "STAMP-23 Sequential",
         .Category = "Release",
         .SelectionMode = StampVariantSelectionMode::Sequential,
         .Revision = 1U,
         .Variants = {
             {.Id = Core::UUID{0x230401U},
              .Stamp = first,
              .Weight = 1.0,
              .DisplayOrder = 0U},
             {.Id = Core::UUID{0x230402U},
              .Stamp = second,
              .Weight = 1.0,
              .DisplayOrder = 1U}}},
        &validation);
    Require(group && validation.IsValid(),
        "Unable to create the release Smart Variant group.");
    return std::move(*group);
}

void RunVariantSmoke()
{
    IsolatedSmokeFixture fixture("variant");
    StampProjectLibraryRepository repository;
    Require(repository.SetProjectRoot(fixture.ProjectRoot()),
        "Unable to configure the variant smoke repository.");
    const StampLibraryResult firstInstalled = repository.Install(
        MakeSimpleStamp(0x230411U, {220U, 45U, 55U, 255U}),
        {.PreferredFileStem = "variant-first"});
    const StampLibraryResult secondInstalled = repository.Install(
        MakeSimpleStamp(0x230412U, {45U, 210U, 75U, 255U}),
        {.PreferredFileStem = "variant-second"});
    Require(firstInstalled.Succeeded() && secondInstalled.Succeeded(),
        "Unable to install the variant smoke assets.");
    const StampVariantGroup group = MakeVariantGroup(
        firstInstalled.Reference, secondInstalled.Reference);
    StampAssetCache cache(repository);
    auto document = MakeDocument(
        fixture.ProjectRoot() / "Assets" / "Models" / "variant.vox",
        false, {24U, 4U, 4U});
    SmokeEditSession editSession(document);
    VoxelEditHistory history;
    StampPlacementSession placement;
    Require(placement.BeginVariantGroup(
                group, cache, 0x23U, document, SmokeDocumentGeneration,
                0U, {2 * Fixed, Fixed, Fixed}).Succeeded,
        "Unable to begin sequential Smart Variant placement.");
    const auto first = placement.PlaceOnce(
        document, SmokeDocumentGeneration, editSession, history);
    Require(first && first.History.StampVariantMetadata &&
            first.History.StampVariantMetadata->VariantId ==
                Core::UUID{0x230401U} &&
            placement.PlacementOrdinal() == 1U,
        "The first exact Smart Variant identity was not stored.");
    Require(placement.SetTarget(
                {8 * Fixed, Fixed, Fixed},
                document, SmokeDocumentGeneration).Succeeded,
        "Unable to move the second Smart Variant preview.");
    const auto second = placement.PlaceOnce(
        document, SmokeDocumentGeneration, editSession, history);
    Require(second && second.History.StampVariantMetadata &&
            second.History.StampVariantMetadata->VariantId ==
                Core::UUID{0x230402U} &&
            second.History.StampVariantMetadata->StampId ==
                secondInstalled.Reference.Id &&
            placement.PlacementOrdinal() == 2U,
        "The second exact Smart Variant identity was not stored.");
    const auto placedVoxel = document.GetVoxel({8, 1, 1});
    Require(placedVoxel.has_value(),
        "The second Smart Variant did not mutate its exact target.");
    const auto undone = history.Undo(editSession);
    Require(undone && undone.StampVariantMetadata &&
            undone.StampVariantMetadata->VariantId ==
                Core::UUID{0x230402U} &&
            !document.GetVoxel({8, 1, 1}),
        "Smart Variant Undo did not expose and remove the exact choice.");
    Require(repository.Remove(secondInstalled.Reference).Succeeded(),
        "Unable to remove the chosen Smart Variant before Redo.");
    static_cast<void>(cache.Invalidate(secondInstalled.Reference));
    const StampAssetCacheMetrics beforeRedo = cache.Metrics();
    const auto redone = history.Redo(editSession);
    const StampAssetCacheMetrics afterRedo = cache.Metrics();
    Require(redone && redone.StampVariantMetadata &&
            redone.StampVariantMetadata->VariantId ==
                Core::UUID{0x230402U} &&
            redone.StampVariantMetadata->StampId ==
                secondInstalled.Reference.Id &&
            document.GetVoxel({8, 1, 1}) == placedVoxel &&
            beforeRedo.LoadAttempts == afterRedo.LoadAttempts &&
            beforeRedo.LoadFailures == afterRedo.LoadFailures,
        "Smart Variant Redo rerolled or reloaded the deleted chosen source.");
    Require(fixture.Cleanup(),
        "The variant smoke left a temporary project or profile artifact.");
}

void RunSmartPlacementSmoke()
{
    auto document = MakeDocument(
        "stamp-23-smart-placement.vox", false, {16U, 6U, 16U});
    const VoxelStamp stamp = MakeSurfaceStamp();
    StampPlacementSession placement;
    const StampFixedPoint manualTarget{2 * Fixed, Fixed, 2 * Fixed};
    const StampSmartPlacementTargetContext wall{
        .HasSurface = true,
        .SurfacePoint = {7 * Fixed, Fixed, 7 * Fixed},
        .SurfaceNormal = {.X = 1}};
    const std::uint64_t revision = document.GetRevision();
    Require(placement.Begin(
                stamp, document, SmokeDocumentGeneration, 0U,
                manualTarget).Succeeded &&
            placement.SmartPlacementEnabled() &&
            placement.SmartPlacementMode() ==
                StampSmartPlacementMode::PreviewAssist,
        "Smart Placement did not start enabled in advisory Preview Assist mode.");
    Require(placement.UpdateSmartPlacementContext(
                wall, document, SmokeDocumentGeneration).Succeeded &&
            placement.CurrentSmartPlacementSuggestion() != nullptr &&
            placement.CurrentSmartPlacementSuggestion()->Available() &&
            placement.SmartPlacementAppliedToPreview() &&
            placement.CurrentPlan()->Transform.TargetPivot == wall.SurfacePoint,
        "Smart Placement did not expose and apply its deterministic wall suggestion.");
    Require(placement.SetQuarterRotation(
                3U, document, SmokeDocumentGeneration).Succeeded &&
            placement.SmartPlacementOrientationLocked() &&
            placement.CurrentPlan()->Transform.QuarterTurns == 3U,
        "An explicit user rotation did not win over Smart Placement.");
    Require(placement.SetSmartPlacementTemporaryBypass(
                true, document, SmokeDocumentGeneration).Succeeded &&
            !placement.SmartPlacementAppliedToPreview() &&
            placement.CurrentPlan()->Transform.TargetPivot == manualTarget &&
            placement.CurrentPlan()->CanCommit,
        "Temporary bypass did not restore the allowed manual placement.");
    Require(placement.SetSmartPlacementTemporaryBypass(
                false, document, SmokeDocumentGeneration).Succeeded &&
            placement.SmartPlacementAppliedToPreview() &&
            placement.CurrentPlan()->Transform.QuarterTurns == 3U,
        "Ending temporary bypass did not restore deterministic assistance.");
    Require(placement.SetSmartPlacementEnabled(
                false, document, SmokeDocumentGeneration).Succeeded &&
            !placement.SmartPlacementAppliedToPreview() &&
            placement.CurrentSmartPlacementSuggestion()->Status ==
                StampSmartPlacementSuggestionStatus::Disabled &&
            placement.CurrentPlan()->Transform.TargetPivot == manualTarget &&
            document.GetRevision() == revision,
        "Global disable changed geometry, forced advice, or mutated the document.");
}

} // namespace

const char* VoxelStampSmokeModeName(const VoxelStampSmokeMode mode) noexcept
{
    switch (mode)
    {
    case VoxelStampSmokeMode::Mvp: return "mvp";
    case VoxelStampSmokeMode::Corruption: return "corruption";
    case VoxelStampSmokeMode::Library: return "library";
    case VoxelStampSmokeMode::Variant: return "variant";
    case VoxelStampSmokeMode::SmartPlacement: return "smart-placement";
    }
    return "unknown";
}

VoxelStampSmokeResult RunVoxelStampSmokeTest(
    const VoxelStampSmokeMode mode) noexcept
{
    VoxelStampSmokeResult result{.Mode = mode};
    try
    {
        switch (mode)
        {
        case VoxelStampSmokeMode::Mvp: RunMvpSmoke(); break;
        case VoxelStampSmokeMode::Corruption: RunCorruptionSmoke(); break;
        case VoxelStampSmokeMode::Library: RunLibrarySmoke(); break;
        case VoxelStampSmokeMode::Variant: RunVariantSmoke(); break;
        case VoxelStampSmokeMode::SmartPlacement: RunSmartPlacementSmoke(); break;
        }
        result.Succeeded = true;
        result.Message = std::string("Voxel Stamp ") +
            VoxelStampSmokeModeName(mode) + " smoke passed.";
    }
    catch (const std::exception& error)
    {
        result.Message = std::string("Voxel Stamp ") +
            VoxelStampSmokeModeName(mode) + " smoke failed: " + error.what();
    }
    catch (...)
    {
        result.Message = std::string("Voxel Stamp ") +
            VoxelStampSmokeModeName(mode) +
            " smoke failed with an unknown exception.";
    }
    return result;
}

} // namespace VoxelForge::Editor::Stamps
