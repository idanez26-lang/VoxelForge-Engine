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

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{16U, 16U, 16U}, {}});
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source,
        "smart-tool-stroke-smoke.vox");
    Require(loaded.Succeeded(), "Unable to create the Smart Tool stroke smoke document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    const Asset::Voxel::VoxelSubModel* const source = document.GetModel(0U);
    Require(source != nullptr, "Smoke document has no voxel sub-model.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to allocate the smoke compatibility grid.");
    Voxel::VoxelModel model;
    model.AddGrid(std::move(grid));
    return model;
}

class SmokeEditSession final : public VoxelEditSession
{
public:
    explicit SmokeEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibilityModel(document)) {}

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return generation_;
    }
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return &model_;
    }
    [[nodiscard]] Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completeCount_; }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 1U;
    std::size_t rebuildCount_ = 0U;
    std::size_t completeCount_ = 0U;
};

SmartToolRequest Request(const SmartToolStroke& stroke, const SmartAction action,
    const SmartToolMode mode, const int size, const std::uint8_t palette,
    const Position target)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = {16U, 16U, 16U};
    request.BrushRequest.State = {SmartBrushShape::Cube,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Auto,
        size, palette, SmartBrushMode::Add};
    request.BrushRequest.Placement = {target, {0, 1, 0}};
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.VirtualRevision = stroke.Revision();
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    request.SourceSubModelIndex = 0U;
    request.ReadVoxel = [&stroke](const Position position)
    {
        return stroke.ReadVoxel(position);
    };
    return request;
}

std::vector<Asset::Voxel::VoxelDocumentChange> BuildStroke(
    Asset::Voxel::VoxelDocument& document, const SmokeEditSession& session,
    const SmartAction action, const SmartToolMode mode, const int size,
    const std::uint8_t palette, const std::vector<Position>& targets)
{
    Require(!targets.empty(), "The smoke stroke needs one target.");
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), session.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, action, targets.front(), {0, 1, 0}),
        "Unable to begin the smoke stroke.");
    SmartToolController controller;
    SmartToolSession plannerSession;
    auto resolve = [&](const Position target)
    {
        const SmartToolResult result = controller.ResolvePreview(plannerSession,
            Request(stroke, action, mode, size, palette, target));
        Require(result.HasPlan() && result.Code == SmartBrushResultCode::Valid,
            "The smoke planner did not produce a valid stroke plan.");
        Require(stroke.Accumulate(*result.Plan),
            "The smoke stroke rejected planner-owned changes.");
    };
    resolve(targets.front());
    for (std::size_t index = 1U; index < targets.size(); ++index)
        for (const Position sample : stroke.Advance(targets[index], {0, 1, 0}))
            resolve(sample);
    return stroke.Changes();
}

void RequireExactPreviewMatchesCommit(const Asset::Voxel::VoxelDocument& source,
    const std::vector<Asset::Voxel::VoxelDocumentChange>& changes,
    const Asset::Voxel::VoxelDocument& committed)
{
    const SmartToolExactPreviewMesh preview =
        SmartToolExactPreviewComposer::Compose(source, changes);
    const Mesh::MeshBuildResult expected = Mesh::VoxelMeshBuilder::Build(committed);
    Require(preview.Succeeded() && expected.Succeeded && expected.Mesh &&
            preview.Mesh.Vertices() == expected.Mesh->Vertices() &&
            preview.Mesh.Indices() == expected.Mesh->Indices(),
        "The stroke smoke preview diverged from the committed mesh.");
}

void Apply(const std::vector<Asset::Voxel::VoxelDocumentChange>& changes,
    Asset::Voxel::VoxelDocument& document, SmokeEditSession& session,
    VoxelEditHistory& history, const SmartAction action, const Position target)
{
    const VoxelToolResult applied = VoxelPencilTool::ApplyChanges(
        {&session, &document, 0U, session.VoxelModelGeneration(), &history,
            std::nullopt, nullptr, nullptr}, action, target, changes);
    Require(applied.Code == VoxelToolResultCode::Applied,
        "The smoke stroke was not committed through one VoxelEditOperation.");
}

void RunSmoke()
{
    Asset::Voxel::VoxelDocument document = MakeDocument();
    SmokeEditSession session(document);
    VoxelEditHistory history;

    // Single Create.
    const auto single = BuildStroke(document, session, SmartAction::Add,
        SmartToolMode::SingleVoxel, 1, 5U, {{2, 2, 2}});
    Require(single.size() == 1U && document.GetVoxelCount() == 0U,
        "Single Create mutated the document before MouseUp.");
    const Asset::Voxel::VoxelDocument beforeSingle = document;
    Apply(single, document, session, history, SmartAction::Add, {2, 2, 2});
    Require(history.UndoCount() == 1U && document.GetVoxelCount() == 1U,
        "Single Create was not one atomic transaction.");
    RequireExactPreviewMatchesCommit(beforeSingle, single, document);

    // Continuous Cube Create: two adjacent centers must create a solid union
    // rather than a sampled sequence with gaps.
    const Asset::Voxel::VoxelDocument beforeCube = document;
    const auto cube = BuildStroke(document, session, SmartAction::Add,
        SmartToolMode::CubeBrush, 3, 6U, {{7, 7, 7}, {8, 7, 7}});
    Require(cube.size() == 36U, "Cube Create did not de-duplicate its overlap.");
    Apply(cube, document, session, history, SmartAction::Add, {8, 7, 7});
    for (const Asset::Voxel::VoxelDocumentChange& change : cube)
        if (change.ExistsAfter)
            Require(document.HasVoxel(change.Position),
                "Continuous Cube Create left a hole in the planned union.");
    Require(history.UndoCount() == 2U,
        "Cube Create appended more than one history operation.");
    RequireExactPreviewMatchesCommit(beforeCube, cube, document);

    // Sphere Paint changes existing cube cells only; no new voxel is created.
    const std::uint64_t countBeforePaint = document.GetVoxelCount();
    const Asset::Voxel::VoxelDocument beforePaint = document;
    const auto paint = BuildStroke(document, session, SmartAction::Paint,
        SmartToolMode::SphereBrush, 3, 12U, {{7, 7, 7}, {8, 7, 7}});
    Require(!paint.empty(), "Sphere Paint did not resolve existing voxels.");
    Apply(paint, document, session, history, SmartAction::Paint, {7, 7, 7});
    Require(document.GetVoxelCount() == countBeforePaint && history.UndoCount() == 3U,
        "Sphere Paint changed occupancy or transaction atomicity.");
    RequireExactPreviewMatchesCommit(beforePaint, paint, document);

    // Cylinder Remove is the fourth atomic transaction.
    const Asset::Voxel::VoxelDocument beforeRemove = document;
    const auto remove = BuildStroke(document, session, SmartAction::Erase,
        SmartToolMode::CylinderBrush, 3, 0U, {{7, 7, 7}, {8, 7, 7}});
    Require(!remove.empty(), "Cylinder Remove did not resolve existing voxels.");
    Apply(remove, document, session, history, SmartAction::Erase, {7, 7, 7});
    const std::uint64_t countAfterRemove = document.GetVoxelCount();
    Require(history.UndoCount() == 4U && countAfterRemove < countBeforePaint,
        "Cylinder Remove was not one atomic removal transaction.");
    RequireExactPreviewMatchesCommit(beforeRemove, remove, document);

    Require(history.Undo(session) && document.GetVoxelCount() == countBeforePaint &&
            history.Redo(session) && document.GetVoxelCount() == countAfterRemove,
        "Undo/Redo did not restore the final cylinder transaction.");

    // Esc and Clear cancel the pending plan, so there is no mutation or
    // additional history entry. Clearing history models the document-clear
    // route after the cancellation boundary without involving ImGui.
    SmartToolStroke pending;
    Require(pending.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), session.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, SmartAction::Add, {12, 12, 12}, {0, 1, 0}),
        "Unable to prepare the cancellation smoke stroke.");
    SmartToolController cancelController;
    SmartToolSession cancelSession;
    const SmartToolResult pendingPlan = cancelController.ResolvePreview(cancelSession,
        Request(pending, SmartAction::Add, SmartToolMode::SingleVoxel, 1, 9U,
            {12, 12, 12}));
    Require(pendingPlan.HasPlan() && pending.Accumulate(*pendingPlan.Plan),
        "Unable to prepare the pending cancellation changes.");
    const std::uint64_t revisionBeforeCancel = document.GetRevision();
    const std::uint64_t countBeforeCancel = document.GetVoxelCount();
    const std::size_t undoBeforeCancel = history.UndoCount();
    pending.Cancel(); // Esc.
    Require(!pending.IsActive() && !pending.HasChanges() &&
            document.GetRevision() == revisionBeforeCancel &&
            document.GetVoxelCount() == countBeforeCancel &&
            history.UndoCount() == undoBeforeCancel,
        "Esc cancellation mutated the document or history.");
    history.Clear(); // ClearViewport cancellation has already occurred.
    Require(history.UndoCount() == 0U && history.RedoCount() == 0U &&
            document.GetRevision() == revisionBeforeCancel &&
            document.GetVoxelCount() == countBeforeCancel,
        "Clear after cancellation mutated the document or retained history.");
}
}

int main()
{
    try
    {
        RunSmoke();
        std::cout << "Smart Tool stroke smoke passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
