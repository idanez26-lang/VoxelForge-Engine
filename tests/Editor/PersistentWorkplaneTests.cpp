#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelCreation/WorkplaneService.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelSave/VoxelDocumentSaveService.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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
            ("VoxelForgePersistentWorkplane-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ModelPath = Root / "Assets" / "Models" / "Maison.vox";
        fs::create_directories(ModelPath.parent_path());
        fs::create_directories(Root / "Cache");
    }
    ~Fixture()
    {
        std::error_code ignored;
        fs::remove_all(Root, ignored);
    }
    fs::path Root;
    fs::path ModelPath;
};

Asset::Voxel::VoxelDocument CreateEmptyDocument(const fs::path& path)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{8U, 4U, 8U}, {}});
    source.DeclaredModelCount = 1U;
    auto built = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, path, "persistent-workplane-test");
    Require(built.Succeeded(), built.Message);
    const auto serialized =
        Asset::Voxel::VoxDocumentWriter{}.Serialize(*built.Document);
    Require(serialized.Succeeded(), serialized.Message);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(serialized.Bytes.data()),
        static_cast<std::streamsize>(serialized.Bytes.size()));
    Require(static_cast<bool>(output), "Unable to write Workplane fixture.");
    output.close();
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Load(path);
    Require(loaded.Succeeded(), loaded.Message);
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    model.SetName("Persistent Workplane");
    const auto dimensions = document.GetDimensions();
    Require(dimensions.has_value(), "Workplane model dimensions are missing.");
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions->X, dimensions->Y, dimensions->Z),
        "Unable to allocate Workplane compatibility grid.");
    model.AddGrid(std::move(grid));
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
        ++RebuildCount;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++CompletionCount; }
    void UpdateVoxelEditSavedState(const bool saved) noexcept override
    {
        Saved = saved;
    }
    Asset::Voxel::VoxelDocument* Document;
    Voxel::VoxelModel Model;
    std::size_t RebuildCount = 0U;
    std::size_t CompletionCount = 0U;
    bool Saved = true;
};

Editor::VoxelRay RayForCell(
    const std::int32_t x,
    const std::int32_t z) noexcept
{
    constexpr Editor::Vec3 center{4.0F, 2.0F, 4.0F};
    return {
        {static_cast<float>(x) + 0.5F - center.X,
         8.0F,
         static_cast<float>(z) + 0.5F - center.Z},
        {0.0F, -1.0F, 0.0F}};
}

Editor::VoxelToolResult PencilOnWorkplane(
    TestSession& session,
    Asset::Voxel::VoxelDocument& document,
    Editor::VoxelEditHistory& history,
    const Asset::Voxel::VoxelPosition position)
{
    Editor::VoxelPencilContext context;
    context.EditSession = &session;
    context.Document = &document;
    context.PaletteIndex = 5U;
    context.History = &history;
    context.WorkplaneTarget = position;
    return Editor::VoxelPencilTool::Apply(context);
}

void TestPersistentWorkplane()
{
    Fixture fixture;
    Asset::Voxel::VoxelDocument document =
        CreateEmptyDocument(fixture.ModelPath);
    TestSession session(document);
    Editor::VoxelEditHistory history;
    history.MarkSavedState(document);
    Editor::WorkplaneService workplane;

    Require(workplane.Definition() == Editor::WorkplaneDefinition{} &&
        workplane.Grid(document) == Editor::WorkplaneGrid{
            {Editor::WorkplaneAxis::Y, 0}, 8U, 8U},
        "The V1 Workplane grid is not the expected Y=0 X/Z grid.");

    const Editor::WorkplaneHit emptyHit = workplane.Intersect(
        document, 0U, RayForCell(1, 1), {4.0F, 2.0F, 4.0F});
    Require(emptyHit.IsValid() &&
        emptyHit.Position == Asset::Voxel::VoxelPosition{1, 0, 1},
        "An empty document did not produce a valid Workplane hit.");
    const auto emptyPreview = Editor::EvaluateVoxelPencilPreview(
        &document, 0U, std::nullopt, true, emptyHit.Position);
    Require(emptyPreview.IsValid(),
        "The empty Workplane target did not produce a Pencil preview.");
    Require(PencilOnWorkplane(
            session, document, history, *emptyHit.Position).Code ==
                Editor::VoxelToolResultCode::Applied &&
        document.HasVoxel({1, 0, 1}) && history.UndoCount() == 1U,
        "The first Workplane voxel was not created through normal history.");

    const Editor::WorkplaneHit persistentHit = workplane.Intersect(
        document, 0U, RayForCell(6, 6), {4.0F, 2.0F, 4.0F});
    Require(persistentHit.IsValid() &&
        persistentHit.Position == Asset::Voxel::VoxelPosition{6, 0, 6},
        "The Workplane disappeared after the first voxel.");
    Require(PencilOnWorkplane(
            session, document, history, *persistentHit.Position).Code ==
                Editor::VoxelToolResultCode::Applied &&
        document.HasVoxel({6, 0, 6}) && history.UndoCount() == 2U,
        "A second independent Workplane group could not be created.");

    Editor::VoxelRaycastHit voxelHit;
    voxelHit.Coordinates = {1U, 0U, 1U};
    voxelHit.Face = Editor::VoxelHitFace::PositiveY;
    voxelHit.SubModelIndex = 0U;
    voxelHit.AdjacentPosition = {1, 1, 1};
    voxelHit.AdjacentWithinBounds = true;
    voxelHit.DocumentRevision = document.GetRevision();
    Editor::VoxelPencilContext voxelContext;
    voxelContext.EditSession = &session;
    voxelContext.Document = &document;
    voxelContext.Hit = voxelHit;
    voxelContext.PaletteIndex = 7U;
    voxelContext.History = &history;
    Require(Editor::VoxelPencilTool::Apply(voxelContext).Code ==
            Editor::VoxelToolResultCode::Applied &&
        document.HasVoxel({1, 1, 1}),
        "Voxel-face Pencil behavior changed when Workplane support was added.");

    const std::uint64_t refusalRevision = document.GetRevision();
    Require(workplane.Intersect(
            document, 0U, RayForCell(1, 1), {4.0F, 2.0F, 4.0F}).Status ==
                Editor::WorkplaneHitStatus::Occupied &&
        PencilOnWorkplane(session, document, history, {1, 0, 1}).Code ==
            Editor::VoxelToolResultCode::TargetOccupied &&
        PencilOnWorkplane(session, document, history, {-1, 0, 0}).Code ==
            Editor::VoxelToolResultCode::TargetOutOfBounds &&
        document.GetRevision() == refusalRevision,
        "Occupied or out-of-bounds Workplane targets changed the document.");
    Require(workplane.Intersect(
            document, 0U, {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}},
            {4.0F, 2.0F, 4.0F}).Status ==
                Editor::WorkplaneHitStatus::Parallel,
        "A parallel ray unexpectedly hit the Workplane.");
    Editor::WorkplaneService futurePlane({Editor::WorkplaneAxis::X, 0});
    Require(!futurePlane.Grid(document) &&
        futurePlane.Intersect(
            document, 0U, RayForCell(2, 2), {4.0F, 2.0F, 4.0F}).Status ==
                Editor::WorkplaneHitStatus::UnsupportedDefinition,
        "A future X Workplane was accidentally enabled in V1.");

    const std::uint64_t beforeUndo = document.GetRevision();
    Require(static_cast<bool>(history.Undo(session)) &&
        !document.HasVoxel({1, 1, 1}) &&
        document.GetRevision() == beforeUndo + 1U && history.CanRedo(),
        "Normal Undo did not remove the latest Pencil voxel.");
    Require(static_cast<bool>(history.Redo(session)) &&
        document.HasVoxel({1, 1, 1}),
        "Normal Redo did not restore the latest Pencil voxel.");

    Editor::VoxelDocumentSaveService save;
    Require(save.SetProjectRoot(fixture.Root),
        "Unable to configure Workplane save service.");
    const std::uint64_t revisionBeforeSave = document.GetRevision();
    const auto saved = save.Save(document, history);
    Require(saved.Succeeded() && !document.IsDirty() &&
        document.GetRevision() == revisionBeforeSave,
        "Normal Save failed for Workplane-created voxels.");
    const auto reopened =
        Asset::Voxel::VoxDocumentLoader{}.Load(fixture.ModelPath);
    Require(reopened.Succeeded() && reopened.Document->GetVoxelCount() == 3U &&
        reopened.Document->HasVoxel({1, 0, 1}) &&
        reopened.Document->HasVoxel({6, 0, 6}) &&
        reopened.Document->HasVoxel({1, 1, 1}),
        "Independent Workplane groups did not persist after reopen.");
    Require(!fs::exists(fixture.ModelPath.string() + ".vfsave.tmp") &&
        !fs::exists(fixture.ModelPath.string() + ".vfsave.bak"),
        "Workplane Save left transaction files behind.");
}
}

int main()
{
    try
    {
        TestPersistentWorkplane();
        std::cout << "Persistent Workplane tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Persistent Workplane tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
