#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelCreation/FirstCreationExperience.h"
#include "VoxelCreation/WorkplaneService.h"
#include "VoxelCreation/VoxelModelCreationService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelSave/VoxelDocumentSaveService.h"
#include "VoxelTools/VoxelEraserTool.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "Thumbnail/VoxThumbnailService.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class Fixture final
{
public:
    Fixture()
    {
        Root = fs::temp_directory_path() /
            ("VoxelForgeFirstCreation-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Models = Root / "Assets" / "Models";
        fs::create_directories(Models);
        fs::create_directories(Root / "Cache");
    }
    ~Fixture()
    {
        std::error_code ignored;
        fs::remove_all(Root, ignored);
    }
    fs::path Root;
    fs::path Models;
};

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("First creation model");
    for (std::size_t index = 0U; index < document.GetModelCount(); ++index)
    {
        const Asset::Voxel::VoxelSubModel* source = document.GetModel(index);
        Require(source != nullptr, "Creation document sub-model is missing.");
        const auto dimensions = source->Dimensions();
        Voxel::VoxelGrid grid;
        Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
            "Unable to allocate creation compatibility grid.");
        source->ForEachVoxel([&grid](
            const Asset::Voxel::VoxelPosition position,
            const Asset::Voxel::Voxel voxel)
        {
            Require(grid.Set(
                static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
                "Unable to populate creation compatibility grid.");
        });
        model.AddGrid(std::move(grid));
    }
    return model;
}

class TestSession final : public Editor::VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : Document(&document), Model(CompatibilityModel(document)) {}
    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &Model; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return Document;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++Rebuilds;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++Completed; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        Saved = saved;
    }
    Asset::Voxel::VoxelDocument* Document;
    Voxel::VoxelModel Model;
    std::size_t Rebuilds = 0U;
    std::size_t Completed = 0U;
    bool Saved = true;
};

Editor::ModelAssetMetadata ReadMetadata(
    const Fixture& fixture,
    const fs::path& model)
{
    Editor::ModelAssetMetadataService service;
    Require(service.SetModelsDirectory(fixture.Models),
        "Unable to configure creation metadata reader.");
    const auto read = service.ReadMetadata(service.MetadataPathFor(model));
    Require(read.Succeeded, read.Message);
    return read.Metadata;
}

void TestValidation()
{
    std::string error;
    Require(!Editor::VoxelModelCreationService::ValidateModelName("", error),
        "Empty creation name must be refused.");
    Require(!Editor::VoxelModelCreationService::ValidateModelName("bad:name", error),
        "Windows-forbidden creation name must be refused.");
    Require(!Editor::VoxelModelCreationService::ValidateModelName("CON", error),
        "Windows-reserved creation name must be refused.");
    Require(!Editor::VoxelModelCreationService::ValidateModelName(
            "COM1.model", error),
        "Windows-reserved creation stems must be refused.");
    Require(Editor::VoxelModelCreationService::ValidateModelName("Maison", error),
        "A normal creation name must be accepted.");
    Require(!Editor::VoxelModelCreationService::ValidateDimensions(
            {0U, 64U, 64U}, error) &&
        !Editor::VoxelModelCreationService::ValidateDimensions(
            {257U, 64U, 64U}, error) &&
        Editor::VoxelModelCreationService::ValidateDimensions(
            {256U, 1U, 256U}, error),
        "Creation dimension limits are incorrect.");
}

fs::path TestCreationCollisionMetadataAndThumbnail(Fixture& fixture)
{
    Editor::VoxThumbnailService thumbnails;
    Require(thumbnails.SetProjectRoot(fixture.Root),
        "Unable to configure creation thumbnails.");
    std::size_t refreshes = 0U;
    std::size_t opens = 0U;
    fs::path selected;
    fs::path opened;
    Editor::VoxelModelCreationService service;
    Require(service.SetProjectRoot(fixture.Root),
        "Unable to configure model creation service.");
    service.SetThumbnailCallback([&thumbnails](const fs::path& path)
    {
        const auto result = thumbnails.Generate(path, true);
        return Editor::VoxelModelCreationStepResult{
            result.Succeeded(), result.Message};
    });
    service.SetAssetBrowserCallback(
        [&](const fs::path& path) { ++refreshes; selected = path; return true; });
    service.SetOpenCallback(
        [&](const fs::path& path) { ++opens; opened = path; return true; });

    const Editor::VoxelModelCreationRequest request{
        "Maison", {64U, 64U, 64U}};
    const auto created = service.CreateModel(request);
    Require(created.Succeeded() &&
        created.Status == Editor::VoxelModelCreationStatus::Created &&
        created.ModelPath.filename() == "Maison.vox" &&
        created.ThumbnailGenerated && created.AssetBrowserRefreshed &&
        created.Opened && refreshes == 1U && opens == 1U &&
        selected == created.ModelPath && opened == created.ModelPath &&
        created.Metadata.Thumbnail &&
        created.Metadata.Thumbnail->Status == Editor::ThumbnailStatus::Valid,
        "Valid model creation did not complete every pipeline step.");
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Load(created.ModelPath);
    Require(loaded.Succeeded() && loaded.Document->GetModelCount() == 1U &&
        loaded.Document->GetVoxelCount() == 0U &&
        loaded.Document->GetDimensions() ==
            Asset::Voxel::VoxelDimensions{64U, 64U, 64U} &&
        !loaded.Document->IsDirty() && loaded.Document->GetRevision() == 0U,
        "New model must be a clean, genuinely empty 64-cube document.");
    const Editor::ModelAssetMetadata initial =
        ReadMetadata(fixture, created.ModelPath);
    Require(initial.Analysis && initial.Analysis->Valid &&
        initial.Analysis->VoxelCount == 0U && initial.Thumbnail &&
        initial.Thumbnail->Status == Editor::ThumbnailStatus::Valid,
        "Creation metadata, analysis, or thumbnail is incomplete.");

    const auto collision = service.CreateModel(request);
    Require(collision.Status == Editor::VoxelModelCreationStatus::Collision,
        "Creation collision must require an explicit decision.");
    const auto cancelled = service.CreateModel(
        request, Editor::VoxelModelCreationCollisionAction::Cancel);
    Require(cancelled.Status == Editor::VoxelModelCreationStatus::Cancelled,
        "Creation collision Cancel was not honored.");
    const auto renamed = service.CreateModel(
        request, Editor::VoxelModelCreationCollisionAction::Rename);
    Require(renamed.Succeeded() &&
        renamed.Status == Editor::VoxelModelCreationStatus::Renamed &&
        renamed.ModelPath.filename() == "Maison (1).vox",
        "Creation collision Rename did not choose the expected path.");
    const auto replaced = service.CreateModel(
        request, Editor::VoxelModelCreationCollisionAction::Replace);
    const Editor::ModelAssetMetadata afterReplace =
        ReadMetadata(fixture, created.ModelPath);
    Require(replaced.Succeeded() &&
        replaced.Status == Editor::VoxelModelCreationStatus::Replaced &&
        afterReplace.AssetId == initial.AssetId,
        "Creation collision Replace did not preserve asset identity.");
    Require(!fs::exists(created.ModelPath.string() + ".vfcreate.tmp") &&
        !fs::exists(created.ModelPath.string() + ".vfcreate.bak"),
        "Creation transaction left temporary files.");
    return created.ModelPath;
}

void TestFirstVoxelUndoRedoSaveAndReopen(
    Fixture& fixture,
    const fs::path& modelPath)
{
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Load(modelPath);
    Require(loaded.Succeeded(), loaded.Message);
    Asset::Voxel::VoxelDocument document = std::move(*loaded.Document);
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    Editor::FirstCreationExperience experience;
    experience.Start(false);
    Require(experience.Stage() == Editor::FirstCreationStage::Welcome,
        "First creation assistant did not start.");
    experience.Acknowledge();

    const Editor::Vec3 center{32.0F, 32.0F, 32.0F};
    const auto workplaneHit = Editor::WorkplaneService{}.Intersect(
        document, 0U, {{0.5F, -16.0F, 0.5F}, {0.0F, -1.0F, 0.0F}}, center);
    const auto target = workplaneHit.Position;
    Require(workplaneHit.IsValid() &&
        target == Asset::Voxel::VoxelPosition{32, 0, 32},
        "Workplane did not resolve the expected first voxel.");
    const auto preview = Editor::EvaluateVoxelPencilPreview(
        &document, 0U, std::nullopt, true, target);
    Require(preview.IsValid() && preview.Position == target,
        "First voxel preview is not valid on the Workplane.");
    Editor::VoxelPencilContext pencil;
    pencil.EditSession = &session;
    pencil.Document = &document;
    pencil.PaletteIndex = 1U;
    pencil.History = &history;
    pencil.WorkplaneTarget = target;
    const auto pencilled = Editor::VoxelPencilTool::Apply(pencil);
    experience.OnFirstVoxelCreated();
    Require(pencilled.Code == Editor::VoxelToolResultCode::Applied &&
        document.GetVoxelCount() == 1U && document.IsDirty() &&
        history.CanUndo() &&
        experience.Stage() == Editor::FirstCreationStage::Undo,
        "First Pencil did not create one undoable voxel.");
    const auto persistentPreview = Editor::EvaluateVoxelPencilPreview(
        &document, 0U, std::nullopt, true,
        Asset::Voxel::VoxelPosition{33, 0, 32});
    Require(persistentPreview.IsValid(),
        "The Workplane must remain available after the first voxel.");

    Require(static_cast<bool>(history.Undo(session)),
        "First voxel Undo failed.");
    experience.OnUndo();
    Require(document.GetVoxelCount() == 0U && history.CanRedo() &&
        experience.Stage() == Editor::FirstCreationStage::Redo,
        "First voxel Undo did not restore the empty document.");
    Require(static_cast<bool>(history.Redo(session)),
        "First voxel Redo failed.");
    experience.OnRedo();
    Require(document.GetVoxelCount() == 1U &&
        experience.Stage() == Editor::FirstCreationStage::Save,
        "First voxel Redo did not restore the voxel.");

    Editor::VoxelDocumentSaveService save;
    Require(save.SetProjectRoot(fixture.Root),
        "Unable to configure first creation Save.");
    const std::uint64_t revision = document.GetRevision();
    const auto saved = save.Save(document, history);
    Require(saved.Succeeded() && !document.IsDirty() &&
        document.GetRevision() == revision && history.CanUndo(),
        "First Ctrl+S semantics are incorrect.");
    Require(experience.OnSave() &&
        experience.Stage() == Editor::FirstCreationStage::Completed,
        "First creation assistant did not complete after Save.");

    loaded = Asset::Voxel::VoxDocumentLoader{}.Load(modelPath);
    Require(loaded.Succeeded() && loaded.Document->GetVoxelCount() == 1U &&
        loaded.Document->HasVoxel(*target),
        "First voxel did not persist after close and reopen.");

    Asset::Voxel::VoxelDocument reopened = std::move(*loaded.Document);
    TestSession eraseSession(reopened);
    Editor::VoxelEditHistory eraseHistory;
    eraseHistory.MarkSavedState(reopened);
    Editor::VoxelRaycastHit hit;
    hit.Coordinates = {
        static_cast<std::uint32_t>(target->X),
        static_cast<std::uint32_t>(target->Y),
        static_cast<std::uint32_t>(target->Z)};
    hit.Face = Editor::VoxelHitFace::PositiveY;
    hit.SubModelIndex = 0U;
    hit.ColorIndex = 1U;
    hit.DocumentRevision = reopened.GetRevision();
    const auto erased = Editor::VoxelEraserTool::Apply({
        &eraseSession, &reopened, 0U, hit, false, &eraseHistory});
    Require(erased.Code == Editor::VoxelEraserResultCode::Applied &&
        reopened.GetVoxelCount() == 0U && !reopened.GetGlobalBounds().HasValue,
        "Removing the first voxel did not restore an empty document.");
}

void TestAssistantAlreadyCompleted()
{
    Editor::FirstCreationExperience experience;
    experience.Start(true);
    Require(!experience.Visible() &&
        experience.Stage() == Editor::FirstCreationStage::Hidden,
        "Completed first creation assistant must never reopen.");
}
}

int main()
{
    try
    {
        TestValidation();
        Fixture fixture;
        const fs::path model =
            TestCreationCollisionMetadataAndThumbnail(fixture);
        TestFirstVoxelUndoRedoSaveAndReopen(fixture, model);
        TestAssistantAlreadyCompleted();
        std::cout << "First creation experience tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "First creation experience tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
