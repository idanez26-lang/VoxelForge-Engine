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
    if (!condition) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{12U, 12U, 12U}, {}});
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "rectangle-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create Smart Rectangle smoke document.");
    return std::move(*loaded.Document);
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document) : document_(&document)
    {
        Voxel::VoxelGrid grid;
        Require(grid.Resize(12U, 12U, 12U), "Unable to allocate smoke grid.");
        model_.AddGrid(std::move(grid));
    }

    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    { return document_; }
    CommandResult RebuildActiveVoxelMesh() override
    { ++rebuilds; return CommandResult::Success(); }
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
    const auto plane = SmartToolPlanner::MakeRectanglePlane(
        pointA, {0, 1, 0}, static_cast<float>(pointA.Y));
    Require(plane.has_value(), "Unable to lock Rectangle smoke plane.");

    SmartToolRequest request;
    request.Geometry = SmartGeometry::Rectangle;
    request.Mode = SmartToolMode::SingleVoxel;
    request.Action = action;
    request.RectanglePlane = *plane;
    request.BrushRequest = {{12U, 12U, 12U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, 1, palette, SmartBrushMode::Add},
        {SmartToolPlanner::ProjectRectangleEndpoint(*plane, pointB), {0, 1, 0}}, {}};
    request.ReadVoxel = [&stroke](const Position position)
    { return stroke.Context().ReadSourceVoxel(position); };
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
    const Position pointB{4, 3, 4};

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

    const auto execute = [&](const SmartAction action, const std::uint8_t palette)
    {
        SmartToolStroke stroke;
        Require(begin(stroke, action), "Unable to begin Rectangle stroke.");
        SmartToolController controller;
        SmartToolSession planning;
        const SmartToolResult preview = controller.ResolvePreview(
            planning, MakeRequest(stroke, action, pointA, pointB, palette));
        Require(preview.HasPlan() && controller.ResolveCommit(planning).Plan == preview.Plan &&
                stroke.ReplaceWithPlan(*preview.Plan),
            "Rectangle Preview/Commit plan diverged.");

        const Asset::Voxel::VoxelDocument before = document;
        Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U,
                &history, std::nullopt, nullptr, nullptr}, action, pointB,
                stroke.Changes()).Code == VoxelToolResultCode::Applied,
            "Rectangle transaction was not applied.");

        const SmartToolExactPreviewMesh exact =
            SmartToolExactPreviewComposer::Compose(before, stroke.Changes());
        const auto committed = Mesh::VoxelMeshBuilder::Build(document);
        Require(exact.Succeeded() && committed.Succeeded &&
                exact.Mesh.Vertices() == committed.Mesh->Vertices() &&
                exact.Mesh.Indices() == committed.Mesh->Indices(),
            "Rectangle exact preview mesh differs from commit mesh.");
    };

    // Add -> Undo -> Redo exercises one atomic rectangle transaction.
    execute(SmartAction::Add, 7U);
    Require(history.UndoCount() == 1U && history.Undo(session) && history.Redo(session),
        "Rectangle Undo/Redo failed.");
    // The same locked-plane pipeline supports Paint and Remove.
    execute(SmartAction::Paint, 9U);
    execute(SmartAction::Erase, 0U);

    // Esc: cancellation retains neither pending cells nor document mutation.
    const std::uint64_t revisionBeforeCancel = document.GetRevision();
    SmartToolStroke cancelled;
    Require(begin(cancelled, SmartAction::Add), "Unable to begin cancel stroke.");
    cancelled.Cancel();
    Require(!cancelled.HasChanges() && document.GetRevision() == revisionBeforeCancel,
        "Esc cancellation mutated the Rectangle document.");

    // Clear: no pending preview/stroke state survives an explicit clear.
    SmartToolStroke cleared;
    Require(begin(cleared, SmartAction::Add), "Unable to begin clear stroke.");
    cleared.Cancel();
    Require(!cleared.IsActive() && !cleared.HasChanges(),
        "Clear did not remove Rectangle stroke state.");
}
} // namespace

int main()
{
    try
    {
        RunSmoke();
        std::cout << "Smart Tool Rectangle smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
