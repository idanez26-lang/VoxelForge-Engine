#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "SmartTools/SmartToolStroke.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const char* const message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{12U, 12U, 12U}, {
        {2U, 3U, 2U, 2U}, {3U, 3U, 2U, 2U},
        {2U, 3U, 3U, 2U}, {3U, 3U, 3U, 2U}}});
    source.DeclaredModelCount = 1U;

    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "surface-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create Smart Surface smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    const Asset::Voxel::VoxelSubModel* source = document.GetModel(0U);
    Require(source != nullptr, "Surface smoke document has no sub-model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to allocate Surface smoke compatibility grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                    static_cast<std::uint32_t>(position.Y),
                    static_cast<std::uint32_t>(position.Z),
                    {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to seed Surface smoke compatibility grid.");
    });
    Voxel::VoxelModel model;
    model.AddGrid(std::move(grid));
    return model;
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document)) {}

    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completions; }

    std::size_t rebuilds = 0U;
    std::size_t completions = 0U;

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

SmartToolRequest MakeRequest(const SmartToolStroke& stroke,
    const SmartAction action, const Position pointA, const Position pointB,
    const std::uint8_t palette)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Surface;
    request.Mode = SmartToolMode::SingleVoxel;
    request.Action = action;
    request.FaceSeed = {pointA, {0, 1, 0}};
    request.BrushRequest = {{12U, 12U, 12U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, 1, palette, SmartBrushMode::Add},
        {pointB, {0, 1, 0}}, {}};
    request.ReadVoxel = [&stroke](const Position position)
    {
        return stroke.Context().ReadSourceVoxel(position);
    };
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    return request;
}

void RunSmoke()
{
    Asset::Voxel::VoxelDocument document = MakeDocument();
    Session session(document);
    VoxelEditHistory history;
    const Position pointA{2, 3, 2};
    const Position extension{4, 3, 2};

    const auto begin = [&](SmartToolStroke& stroke, const SmartAction action)
    {
        return stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
                document.GetRevision(), 1U, 0U,
                [&document](const Position position)
                {
                    const auto voxel = document.GetVoxel(position);
                    return SmartToolVoxelState{voxel.has_value(),
                        voxel ? voxel->PaletteIndex : 0U};
                }}, action, pointA, {0, 1, 0});
    };

    const auto execute = [&](const SmartAction action, const std::uint8_t palette,
        const Position target)
    {
        SmartToolStroke stroke;
        Require(begin(stroke, action), "Unable to begin Surface stroke.");

        SmartToolController controller;
        SmartToolSession planning;
        const SmartToolResult preview = controller.ResolvePreview(
            planning, MakeRequest(stroke, action, pointA, target, palette));
        Require(preview.HasPlan() &&
                controller.ResolveCommit(planning).Plan == preview.Plan &&
                stroke.ReplaceWithPlan(*preview.Plan),
            "Surface Preview/Commit plan diverged.");

        const Asset::Voxel::VoxelDocument before = document;
        Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U,
                &history, std::nullopt, nullptr, nullptr}, action, target,
                stroke.Changes()).Code == VoxelToolResultCode::Applied,
            "Surface transaction was not applied.");

        const SmartToolExactPreviewMesh exact =
            SmartToolExactPreviewComposer::Compose(before, stroke.Changes());
        const auto committed = Mesh::VoxelMeshBuilder::Build(document);
        Require(exact.Succeeded() && committed.Succeeded &&
                exact.Mesh.Vertices() == committed.Mesh->Vertices() &&
                exact.Mesh.Indices() == committed.Mesh->Indices(),
            "Surface exact preview mesh differs from commit mesh.");
    };

    // Add -> Paint -> Remove each create one atomic document transaction.
    execute(SmartAction::Add, 7U, extension);
    Require(history.UndoCount() == 1U && history.Undo(session) && history.Redo(session),
        "Surface Undo/Redo failed.");
    execute(SmartAction::Paint, 9U, pointA);
    execute(SmartAction::Erase, 0U, pointA);

    // Esc: cancellation retains neither pending cells nor document mutation.
    const std::uint64_t revisionBeforeCancel = document.GetRevision();
    SmartToolStroke cancelled;
    Require(begin(cancelled, SmartAction::Add), "Unable to begin cancel stroke.");
    cancelled.Cancel();
    Require(!cancelled.HasChanges() && document.GetRevision() == revisionBeforeCancel,
        "Esc cancellation mutated the Surface document.");

    // Clear: no pending preview/stroke state survives an explicit clear.
    SmartToolStroke cleared;
    Require(begin(cleared, SmartAction::Add), "Unable to begin clear stroke.");
    cleared.Cancel();
    Require(!cleared.IsActive() && !cleared.HasChanges(),
        "Clear did not remove Surface stroke state.");
}
} // namespace

int main()
{
    try
    {
        RunSmoke();
        std::cout << "Smart Tool Surface smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
