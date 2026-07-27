#include "Commands/Voxel/VoxelEditSession.h"
#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "SmartTools/SmartToolLineConstraintResolver.h"
#include "SmartTools/SmartToolStroke.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{16U, 16U, 16U}, {}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source,
        "smart-line-constraint-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create Line constraint smoke document.");
    return std::move(*loaded.Document);
}

class Session final : public VoxelEditSession
{
public:
    explicit Session(Asset::Voxel::VoxelDocument& document) : document_(&document)
    {
        Voxel::VoxelGrid grid;
        Require(grid.Resize(16U, 16U, 16U), "Grid allocation failed.");
        model_.AddGrid(std::move(grid));
    }

    std::uint64_t VoxelModelGeneration() const noexcept override { return 1U; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    { return document_; }
    CommandResult RebuildActiveVoxelMesh() override
    { ++rebuilds; return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override { ++completions; }

private:
    Asset::Voxel::VoxelDocument* document_;
    Voxel::VoxelModel model_;
    std::size_t rebuilds = 0U;
    std::size_t completions = 0U;
};

SmartToolRequest MakeRequest(const SmartToolStroke& stroke,
    const SmartAction action, const Position pointA, const Position pointB,
    const SmartToolMode mode = SmartToolMode::SingleVoxel,
    const std::uint8_t palette = 11U)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Line;
    request.Mode = mode;
    request.Action = action;
    request.LineStart = pointA;
    const SmartBrushMode brushMode = action == SmartAction::Erase
        ? SmartBrushMode::Erase : action == SmartAction::Paint
        ? SmartBrushMode::Paint : SmartBrushMode::Add;
    request.BrushRequest = {{16U, 16U, 16U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, mode == SmartToolMode::SingleVoxel ? 1 : 3,
            palette, brushMode}, {pointB, {0, 1, 0}}, {}};
    request.ReadVoxel = [&stroke](const Position position)
    { return stroke.Context().ReadSourceVoxel(position); };
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    request.SourceSubModelIndex = stroke.Context().SubModelIndex;
    return request;
}

SmartToolPlanPtr ResolvePlan(const SmartToolStroke& stroke,
    const SmartAction action, const Position pointA, const Position pointB,
    const SmartToolMode mode = SmartToolMode::SingleVoxel,
    const std::uint8_t palette = 11U)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult preview = controller.ResolvePreview(session,
        MakeRequest(stroke, action, pointA, pointB, mode, palette));
    Require(preview.HasPlan() && controller.ResolveCommit(session).Plan == preview.Plan,
        "Constrained preview and commit did not share one plan.");
    return preview.Plan;
}

bool Begin(SmartToolStroke& stroke, Asset::Voxel::VoxelDocument& document,
    const SmartAction action, const Position pointA)
{
    return stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), 1U, 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, action, pointA, {0, 1, 0});
}

void RunSmoke()
{
    Asset::Voxel::VoxelDocument document = MakeDocument();
    Session session(document);
    VoxelEditHistory history;
    const Position pointA{2, 2, 2};
    SmartToolLineConstraintResolver constraint;

    // Free Line, then Shift pressed and released during one drag.
    constraint.Begin(pointA);
    const auto free = constraint.Resolve({6, 4, 3}, false);
    const auto constrainedX = constraint.Resolve({6, 4, 3}, true);
    const auto released = constraint.Resolve({6, 4, 3}, false);
    Require(!free.Axis && constrainedX.Axis == SmartToolLineAxis::X &&
            constrainedX.Endpoint == Position{6, 2, 2} && !released.Axis &&
            released.Endpoint == free.Endpoint,
        "Shift press/release did not transition Line endpoints correctly.");

    const auto resolveAxis = [&constraint, pointA](const Position freeEndpoint,
        const SmartToolLineAxis expected, const Position constrainedEndpoint)
    {
        constraint.Begin(pointA);
        const auto result = constraint.Resolve(freeEndpoint, true);
        Require(result.Axis == expected && result.Endpoint == constrainedEndpoint,
            "Constrained Line did not retain its expected world axis.");
        return result.Endpoint;
    };
    const Position lineX = resolveAxis({7, 4, 3}, SmartToolLineAxis::X, {7, 2, 2});
    const Position lineY = resolveAxis({3, 8, 4}, SmartToolLineAxis::Y, {2, 8, 2});
    const Position lineZ = resolveAxis({3, 4, 9}, SmartToolLineAxis::Z, {2, 2, 9});

    SmartToolStroke add;
    Require(Begin(add, document, SmartAction::Add, pointA), "Constrained Add stroke failed.");
    const SmartToolPlanPtr addPlan = ResolvePlan(add, SmartAction::Add, pointA, lineX);
    Require(add.ReplaceWithPlan(*addPlan), "Constrained Add plan replacement failed.");
    const auto before = document;
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U, &history,
            std::nullopt, nullptr, nullptr}, SmartAction::Add, lineX, add.Changes()).Code ==
            VoxelToolResultCode::Applied && history.UndoCount() == 1U,
        "Constrained Add did not create one atomic history entry.");
    const auto preview = SmartToolExactPreviewComposer::Compose(before, add.Changes());
    const auto committedMesh = Mesh::VoxelMeshBuilder::Build(document);
    Require(preview.Succeeded() && committedMesh.Succeeded &&
            preview.Mesh.Vertices() == committedMesh.Mesh->Vertices() &&
            preview.Mesh.Indices() == committedMesh.Mesh->Indices(),
        "Constrained Line preview differs from committed mesh.");

    // Existing shapes stay in SmartBrushEngine; the resolver only supplies B.
    for (const SmartToolMode mode : {SmartToolMode::CubeBrush,
            SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush})
    {
        SmartToolStroke shaped;
        Require(Begin(shaped, document, SmartAction::Add, pointA),
            "Constrained shaped stroke failed.");
        const SmartToolPlanPtr plan = ResolvePlan(shaped, SmartAction::Add,
            pointA, lineY, mode);
        Require(plan->Cells().size() > 1U,
            "Constrained Line did not reuse the active Smart Brush shape.");
        shaped.Cancel();
    }

    SmartToolStroke paint;
    Require(Begin(paint, document, SmartAction::Paint, pointA), "Constrained Paint stroke failed.");
    const SmartToolPlanPtr paintPlan = ResolvePlan(paint, SmartAction::Paint,
        pointA, lineX, SmartToolMode::SingleVoxel, 12U);
    Require(paint.ReplaceWithPlan(*paintPlan) &&
            VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U, &history,
                std::nullopt, nullptr, nullptr}, SmartAction::Paint, lineX,
                paint.Changes()).Code == VoxelToolResultCode::Applied,
        "Constrained Paint failed.");
    SmartToolStroke erase;
    Require(Begin(erase, document, SmartAction::Erase, pointA), "Constrained Remove stroke failed.");
    const SmartToolPlanPtr erasePlan = ResolvePlan(erase, SmartAction::Erase, pointA, lineX);
    Require(erase.ReplaceWithPlan(*erasePlan) &&
            VoxelPencilTool::ApplyChanges({&session, &document, 0U, 1U, &history,
                std::nullopt, nullptr, nullptr}, SmartAction::Erase, lineX,
                erase.Changes()).Code == VoxelToolResultCode::Applied,
        "Constrained Remove failed.");
    Require(history.Undo(session) && history.Redo(session),
        "Constrained Line Undo/Redo failed.");

    const std::uint64_t revision = document.GetRevision();
    SmartToolStroke cancelled;
    Require(Begin(cancelled, document, SmartAction::Add, pointA), "Constrained Esc stroke failed.");
    const SmartToolPlanPtr cancelledPlan = ResolvePlan(cancelled, SmartAction::Add,
        pointA, lineZ);
    Require(cancelled.ReplaceWithPlan(*cancelledPlan), "Constrained Esc plan failed.");
    cancelled.Cancel();
    constraint.Reset();
    Require(document.GetRevision() == revision && !cancelled.HasChanges() &&
            !constraint.IsActive(), "Esc did not clean constrained Line state.");

    history.Clear();
    Require(history.UndoCount() == 0U && history.RedoCount() == 0U,
        "Clear did not release the constrained Line history state.");
}
}

int main()
{
    try
    {
        RunSmoke();
        std::cout << "Smart Tool Line constraint smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
