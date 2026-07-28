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
    const std::uint8_t palette,
    const SmartToolMode mode = SmartToolMode::SingleVoxel,
    const int height = 1, const int brushSize = 1)
{
    const auto plane = SmartToolPlanner::MakeGeometryPlane(
        pointA, {0, 1, 0}, static_cast<float>(pointA.Y));
    Require(plane.has_value(), "Unable to lock Rectangle smoke plane.");

    SmartToolRequest request;
    request.Geometry = SmartGeometry::Geometry;
    request.GeometryHeight = height;
    request.Mode = mode;
    request.Action = action;
    request.GeometryPlane = *plane;
    request.BrushRequest = {{12U, 12U, 12U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, brushSize, palette, SmartBrushMode::Add},
        {SmartToolPlanner::ProjectGeometryEndpoint(*plane, pointB), {0, 1, 0}}, {}};
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

    const auto execute = [&](const SmartAction action,
        const std::uint8_t palette,
        const SmartToolMode mode = SmartToolMode::SingleVoxel,
        const int brushSize = 1)
    {
        SmartToolStroke stroke;
        Require(begin(stroke, action), "Unable to begin Rectangle stroke.");
        SmartToolController controller;
        SmartToolSession planning;
        const SmartToolResult preview = controller.ResolvePreview(
            planning, MakeRequest(stroke, action, pointA, pointB, palette,
                mode, 1, brushSize));
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
    // Circle uses the same immutable Preview -> Commit transaction.
    execute(SmartAction::Add, 7U, SmartToolMode::SphereBrush, 9);
    execute(SmartAction::Erase, 0U, SmartToolMode::SphereBrush, 9);

    // Cylinder phase 1 locks/replaces only the base preview: MouseUp does not
    // touch the document. Phase 2 replaces that preview with the exact signed
    // height plan, and the second click applies one atomic transaction.
    const Position cylinderPointB{4, 3, 2};
    SmartToolStroke cylinder;
    Require(begin(cylinder, SmartAction::Add),
        "Unable to begin Cylinder two-phase stroke.");
    SmartToolController cylinderController;
    SmartToolSession cylinderPlanning;
    const std::uint64_t cylinderRevisionBefore = document.GetRevision();
    const std::size_t cylinderUndoBefore = history.UndoCount();
    const SmartToolResult base = cylinderController.ResolvePreview(
        cylinderPlanning, MakeRequest(cylinder, SmartAction::Add,
            pointA, cylinderPointB, 7U,
            SmartToolMode::CylinderBrush, 1, 9));
    Require(base.HasPlan() && base.Plan->Cells().size() == 13U &&
            base.Plan->Bounds().Dimensions.Y == 1U &&
            cylinder.ReplaceWithPlan(*base.Plan) &&
            document.GetRevision() == cylinderRevisionBefore,
        "Cylinder first MouseUp committed instead of locking its base.");
    const SmartToolResult height = cylinderController.ResolvePreview(
        cylinderPlanning, MakeRequest(cylinder, SmartAction::Add,
            pointA, cylinderPointB, 7U,
            SmartToolMode::CylinderBrush, 3, 9));
    Require(height.HasPlan() && height.Plan->GeometryHeight() == 3 &&
            cylinder.ReplaceWithPlan(*height.Plan) &&
            cylinder.Changes().size() == 39U &&
            document.GetRevision() == cylinderRevisionBefore,
        "Cylinder height phase did not replace the base with the exact plan.");
    const Asset::Voxel::VoxelDocument cylinderBefore = document;
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U,
            &history, std::nullopt, nullptr, nullptr}, SmartAction::Add,
            cylinderPointB,
            cylinder.Changes()).Code == VoxelToolResultCode::Applied &&
            history.UndoCount() == cylinderUndoBefore + 1U,
        "Cylinder second click did not create one atomic transaction.");
    const SmartToolExactPreviewMesh cylinderExact =
        SmartToolExactPreviewComposer::Compose(
            cylinderBefore, cylinder.Changes());
    const auto cylinderCommitted = Mesh::VoxelMeshBuilder::Build(document);
    Require(cylinderExact.Succeeded() && cylinderCommitted.Succeeded &&
            cylinderExact.Mesh.Vertices() ==
                cylinderCommitted.Mesh->Vertices() &&
            cylinderExact.Mesh.Indices() ==
                cylinderCommitted.Mesh->Indices(),
        "Cylinder exact preview mesh differs from commit mesh.");

    // Negative height remains independent from a larger brush size. Esc in
    // the height phase retains neither pending cells nor document mutation.
    const std::uint64_t revisionBeforeCancel = document.GetRevision();
    SmartToolStroke cancelled;
    Require(begin(cancelled, SmartAction::Add), "Unable to begin cancel stroke.");
    const SmartToolResult cancelBase = cylinderController.ResolvePreview(
        cylinderPlanning, MakeRequest(cancelled, SmartAction::Add,
            pointA, cylinderPointB, 7U,
            SmartToolMode::CylinderBrush, 1, 3));
    Require(cancelBase.HasPlan() && cancelled.ReplaceWithPlan(*cancelBase.Plan),
        "Unable to prepare Cylinder base before Esc.");
    const SmartToolResult cancelHeight = cylinderController.ResolvePreview(
        cylinderPlanning, MakeRequest(cancelled, SmartAction::Add,
            pointA, cylinderPointB, 7U,
            SmartToolMode::CylinderBrush, -2, 3));
    Require(cancelHeight.HasPlan() &&
            cancelHeight.Plan->GeometryHeight() == -2 &&
            cancelHeight.Plan->BrushState().Size == 3 &&
            cancelled.ReplaceWithPlan(*cancelHeight.Plan) &&
            document.GetRevision() == revisionBeforeCancel,
        "Negative Cylinder height depended on brush size or mutated early.");
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
