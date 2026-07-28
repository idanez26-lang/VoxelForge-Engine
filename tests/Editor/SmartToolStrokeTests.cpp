#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "SmartTools/SmartToolStroke.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelTools/VoxelPencilTool.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

struct PositionHash final
{
    [[nodiscard]] std::size_t operator()(const Position position) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(position.X)) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Y)) << 11U) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Z)) << 22U);
    }
};
using States = std::unordered_map<Position, SmartToolVoxelState, PositionHash>;

struct StrokeOccupancyContext final
{
    const SmartToolStroke* Stroke = nullptr;
};

std::optional<std::uint8_t> ReadStrokeOccupancy(
    const void* const context, const Position position) noexcept
{
    const auto* const occupancy = static_cast<const StrokeOccupancyContext*>(context);
    if (occupancy == nullptr || occupancy->Stroke == nullptr) return std::nullopt;
    try
    {
        const SmartToolVoxelState voxel = occupancy->Stroke->ReadVoxel(position);
        return voxel.Exists ? std::optional<std::uint8_t>(voxel.PaletteIndex)
                            : std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void Require(bool condition, std::string_view message);

Asset::Voxel::VoxelDocument Document(std::vector<Asset::Vox::VoxVoxel> voxels,
    const Asset::Vox::VoxDimensions dimensions = {16U, 16U, 16U})
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({dimensions, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(source,
        "smart-tool-stroke-memory.vox");
    Require(loaded.Succeeded(), "Unable to create Smart Tool stroke test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel CompatibilityModel(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    const Asset::Voxel::VoxelSubModel* const source = document.GetModel(0U);
    Require(source != nullptr, "The stroke test document has no grid.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the stroke compatibility grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
                static_cast<std::uint32_t>(position.Y),
                static_cast<std::uint32_t>(position.Z),
                {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to seed the stroke compatibility grid.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class TestEditSession final : public VoxelEditSession
{
public:
    explicit TestEditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(CompatibilityModel(document)) {}

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
        ++rebuilds_;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedEdits_; }

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::uint64_t generation_ = 1U;
    std::size_t rebuilds_ = 0U;
    std::size_t completedEdits_ = 0U;
};

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

SmartToolRequest Request(const SmartToolStroke& stroke, const SmartAction action,
    const Position target, const Position normal = {0, 1, 0},
    const SmartToolMode mode = SmartToolMode::SingleVoxel, const int size = 1,
    const std::uint8_t palette = 7U,
    const SmartBrushDimension dimension = SmartBrushDimension::Volume3D,
    const SmartBrushOrientation orientation = SmartBrushOrientation::Auto)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = {16U, 16U, 16U};
    request.BrushRequest.State = {SmartBrushShape::Cube,
        dimension, orientation,
        size, palette, SmartBrushMode::Add};
    request.BrushRequest.Placement = {target, normal};
    request.SourceIdentity = stroke.Context().DocumentIdentity;
    request.SourceRevision = stroke.Context().DocumentRevision;
    request.VirtualRevision = stroke.Revision();
    request.SourceGeneration = stroke.Context().DocumentGeneration;
    request.SourceSubModelIndex = stroke.Context().SubModelIndex;
    request.ReadVoxel = [&stroke](const Position position)
    {
        return stroke.ReadVoxel(position);
    };
    return request;
}

SmartToolStroke Begin(const States& source, const SmartAction action = SmartAction::Add,
    const Position target = {4, 4, 4})
{
    SmartToolStroke stroke;
    const bool begun = stroke.Begin({0x51U, 9U, 3U, 0U,
        [&source](const Position position)
        {
            const auto found = source.find(position);
            return found == source.end() ? SmartToolVoxelState{} : found->second;
        }}, action, target, {0, 1, 0});
    Require(begun, "Unable to begin Smart Tool stroke.");
    return stroke;
}

bool PlanAndAccumulate(SmartToolStroke& stroke, const SmartAction action,
    const Position target, const Position normal = {0, 1, 0},
    const SmartToolMode mode = SmartToolMode::SingleVoxel, const int size = 1,
    const std::uint8_t palette = 7U,
    const SmartBrushDimension dimension = SmartBrushDimension::Volume3D,
    const SmartBrushOrientation orientation = SmartBrushOrientation::Auto)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session,
        Request(stroke, action, target, normal, mode, size, palette,
            dimension, orientation));
    return result.HasPlan() && stroke.Accumulate(*result.Plan);
}

bool SameChanges(const std::vector<Asset::Voxel::VoxelDocumentChange>& left,
    const std::vector<Asset::Voxel::VoxelDocumentChange>& right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        if (left[index].SubModelIndex != right[index].SubModelIndex ||
            left[index].Position != right[index].Position ||
            left[index].ExistedBefore != right[index].ExistedBefore ||
            left[index].PaletteIndexBefore != right[index].PaletteIndexBefore ||
            left[index].ExistsAfter != right[index].ExistsAfter ||
            left[index].PaletteIndexAfter != right[index].PaletteIndexAfter)
            return false;
    }
    return true;
}

std::vector<Asset::Voxel::VoxelDocumentChange> AccumulatePath(
    const std::vector<Position>& anchors)
{
    Require(!anchors.empty(), "A stroke path requires an initial anchor.");
    const States empty;
    SmartToolStroke stroke = Begin(empty, SmartAction::Add, anchors.front());
    Require(PlanAndAccumulate(stroke, SmartAction::Add, anchors.front()),
        "Unable to plan the first path sample.");
    for (std::size_t anchor = 1U; anchor < anchors.size(); ++anchor)
    {
        for (const Position sample : stroke.Advance(anchors[anchor], {0, 1, 0}))
            Require(PlanAndAccumulate(stroke, SmartAction::Add, sample),
                "Unable to plan an interpolated path sample.");
    }
    return stroke.Changes();
}

void TestDeterministicInterpolation()
{
    const auto horizontal = SmartToolStrokeInterpolator::Sample({1, 2, 3}, {6, 2, 3});
    Require(horizontal.size() == 6U && horizontal.front() == Position{1, 2, 3} &&
            horizontal.back() == Position{6, 2, 3},
        "Horizontal stroke interpolation omitted an endpoint.");
    const auto vertical = SmartToolStrokeInterpolator::Sample({2, 1, 2}, {2, 6, 2});
    const auto diagonal = SmartToolStrokeInterpolator::Sample({1, 1, 1}, {6, 4, 3});
    Require(vertical.size() == 6U && diagonal.front() == Position{1, 1, 1} &&
            diagonal.back() == Position{6, 4, 3},
        "Vertical or diagonal interpolation was not deterministic.");
    for (std::size_t index = 1U; index < diagonal.size(); ++index)
    {
        const Position previous = diagonal[index - 1U];
        const Position current = diagonal[index];
        Require(std::abs(current.X - previous.X) <= 1 &&
                std::abs(current.Y - previous.Y) <= 1 &&
                std::abs(current.Z - previous.Z) <= 1,
            "Interpolation introduced a voxel gap.");
    }

    const auto directHorizontal = AccumulatePath({{1, 3, 2}, {9, 3, 2}});
    const auto subdividedHorizontal = AccumulatePath(
        {{1, 3, 2}, {3, 3, 2}, {6, 3, 2}, {9, 3, 2}});
    const auto directVertical = AccumulatePath({{4, 1, 3}, {4, 9, 3}});
    const auto subdividedVertical = AccumulatePath(
        {{4, 1, 3}, {4, 4, 3}, {4, 6, 3}, {4, 9, 3}});
    const auto directDiagonal = AccumulatePath({{1, 1, 1}, {9, 9, 9}});
    const auto subdividedDiagonal = AccumulatePath(
        {{1, 1, 1}, {4, 4, 4}, {6, 6, 6}, {9, 9, 9}});
    Require(SameChanges(directHorizontal, subdividedHorizontal) &&
            SameChanges(directVertical, subdividedVertical) &&
            SameChanges(directDiagonal, subdividedDiagonal),
        "Direct and sub-sampled stroke paths produced different final changes.");
}

void TestStationarySampleAndPreviewCommitAgreement()
{
    const States empty;
    SmartToolStroke stroke = Begin(empty, SmartAction::Add, {4, 4, 4});
    const std::uint64_t revision = stroke.Revision();
    Require(stroke.Advance({4, 4, 4}, {0, 1, 0}).empty() &&
            stroke.Revision() == revision,
        "A stationary pointer sample changed the stroke revision.");

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session,
        Request(stroke, SmartAction::Add, {4, 4, 4}));
    Require(result.HasPlan(), "Unable to create the preview plan.");
    const auto preview = stroke.PreviewChanges(*result.Plan);
    Require(stroke.Accumulate(*result.Plan),
        "Unable to accumulate the preview plan for commit.");
    Require(SameChanges(preview, stroke.Changes()),
        "Preview changes differ from the changes committed by the stroke.");
}

void TestAtomicCommitUndoRedoAndExactAggregatePreview()
{
    Asset::Voxel::VoxelDocument document = Document({});
    TestEditSession session(document);
    VoxelEditHistory history;
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), session.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, SmartAction::Add, {3, 3, 3}, {0, 1, 0}),
        "Unable to begin the atomic stroke.");
    Require(PlanAndAccumulate(stroke, SmartAction::Add, {3, 3, 3}) &&
            PlanAndAccumulate(stroke, SmartAction::Add, {4, 3, 3}),
        "Unable to accumulate the atomic stroke.");
    const std::vector<Asset::Voxel::VoxelDocumentChange> changes = stroke.Changes();
    const SmartToolExactPreviewMesh preview =
        SmartToolExactPreviewComposer::Compose(document, changes);
    Require(preview.Active && preview.Succeeded(),
        "The accumulated stroke did not compose an exact preview mesh.");

    const std::uint64_t revision = document.GetRevision();
    const VoxelToolResult applied = VoxelPencilTool::ApplyChanges(
        {&session, &document, 0U, session.VoxelModelGeneration(), &history,
            std::nullopt, nullptr, nullptr}, SmartAction::Add, {4, 3, 3},
        changes);
    Require(applied.Code == VoxelToolResultCode::Applied &&
            document.GetRevision() == revision + 1U && history.UndoCount() == 1U &&
            history.RedoCount() == 0U && session.completedEdits_ == 1U &&
            session.rebuilds_ == 1U,
        "A complete stroke was not committed as one atomic edit operation.");
    const Mesh::MeshBuildResult committed = Mesh::VoxelMeshBuilder::Build(document);
    Require(committed.Succeeded && committed.Mesh &&
            preview.Mesh.Vertices() == committed.Mesh->Vertices() &&
            preview.Mesh.Indices() == committed.Mesh->Indices(),
        "The aggregate exact preview mesh differs from the post-commit mesh.");
    Require(history.Undo(session) && document.GetVoxelCount() == 0U &&
            history.UndoCount() == 0U && history.RedoCount() == 1U &&
            history.Redo(session) && document.GetVoxelCount() == changes.size() &&
            history.UndoCount() == 1U && history.RedoCount() == 0U,
        "Undo/Redo did not restore the one atomic stroke transaction.");
}

void TestSingleClickIsOneAtomicHistoryOperation()
{
    Asset::Voxel::VoxelDocument document = Document({});
    TestEditSession session(document);
    VoxelEditHistory history;
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), session.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, SmartAction::Add, {2, 2, 2}, {0, 1, 0}) &&
            PlanAndAccumulate(stroke, SmartAction::Add, {2, 2, 2}),
        "Unable to prepare the single-click stroke.");
    const auto changes = stroke.Changes();
    Require(changes.size() == 1U && document.GetVoxelCount() == 0U,
        "A single click prepared an incorrect pending state or mutated the document.");
    Require(VoxelPencilTool::ApplyChanges(
                {&session, &document, 0U, session.VoxelModelGeneration(), &history,
                    std::nullopt, nullptr, nullptr},
                SmartAction::Add, {2, 2, 2}, changes).Code ==
            VoxelToolResultCode::Applied && history.UndoCount() == 1U &&
            history.Undo(session) && document.GetVoxelCount() == 0U &&
            history.Redo(session) && document.GetVoxelCount() == 1U,
        "A single click was not preserved as exactly one Undo/Redo transaction.");
}

void TestCreateDeduplicationAndRevisit()
{
    const States empty;
    SmartToolStroke stroke = Begin(empty);
    Require(PlanAndAccumulate(stroke, SmartAction::Add, {4, 4, 4}) &&
            PlanAndAccumulate(stroke, SmartAction::Add, {5, 4, 4}) &&
            PlanAndAccumulate(stroke, SmartAction::Add, {4, 4, 4}),
        "The planner could not accumulate a simple create stroke.");
    const auto changes = stroke.Changes();
    Require(changes.size() == 2U && !changes[0].ExistedBefore && changes[0].ExistsAfter &&
            !changes[1].ExistedBefore && changes[1].ExistsAfter,
        "Revisiting a create target produced duplicate or inconsistent changes.");
}

void TestVirtualPickingStacksAndContinuousExtension()
{
    Asset::Voxel::VoxelDocument document = Document({{4U, 4U, 3U, 2U}});
    TestEditSession session(document);
    VoxelEditHistory history;
    const Position first{4, 4, 4};
    const Position normal{0, 0, 1};
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
            document.GetRevision(), session.VoxelModelGeneration(), 0U,
            [&document](const Position position)
            {
                const auto voxel = document.GetVoxel(position);
                return SmartToolVoxelState{voxel.has_value(),
                    voxel ? voxel->PaletteIndex : 0U};
            }}, SmartAction::Add, first, normal),
        "Unable to begin the virtual picking stroke.");

    SmartToolController controller;
    SmartToolSession planning;
    const auto resolve = [&](const Position target) -> SmartToolPlanPtr
    {
        const SmartToolResult result = controller.ResolvePreview(
            planning, Request(stroke, SmartAction::Add, target, normal));
        Require(result.HasPlan(), "Unable to build the stacking preview plan.");
        return result.Plan;
    };

    const SmartToolPlanPtr firstPlan = resolve(first);
    Require(stroke.Accumulate(*firstPlan), "Unable to accumulate the first stacked voxel.");
    const StrokeOccupancyContext virtualOccupancy{&stroke};
    const auto virtualHit = RaycastVoxelDocumentWithOccupancy(
        document, {{4.5F, 4.5F, 20.0F}, {0.0F, 0.0F, -1.0F}},
        {&virtualOccupancy, ReadStrokeOccupancy});
    Require(virtualHit && virtualHit->Coordinates ==
            VoxelCoordinates{4U, 4U, 4U} &&
            virtualHit->Face == VoxelHitFace::PositiveZ &&
            virtualHit->AdjacentPosition == Position{4, 4, 5},
        "The pending voxel did not immediately become the next visible picking face.");

    const Position stackedTarget = virtualHit->AdjacentPosition;
    const std::vector<Position> vertical = stroke.Advance(stackedTarget, normal);
    Require(vertical == std::vector<Position>{stackedTarget},
        "A one-cell virtual stack was not sampled without a large screen movement.");
    const SmartToolPlanPtr stackedPlan = resolve(stackedTarget);
    Require(stackedPlan->Placement().Target == stackedTarget &&
            stackedPlan->PlanId() != firstPlan->PlanId() &&
            stroke.Accumulate(*stackedPlan),
        "The plan cache retained the stale first stacking target.");

    const std::vector<Position> lateral = stroke.Advance({6, 4, 5}, normal);
    Require(lateral == std::vector<Position>{{5, 4, 5}, {6, 4, 5}},
        "Continuous lateral Pencil motion was not interpolated from the virtual face.");
    for (const Position target : lateral)
        Require(stroke.Accumulate(*resolve(target)),
            "Unable to accumulate a continuous lateral Pencil sample.");
    Require(stroke.Advance({6, 4, 5}, normal).empty() &&
            stroke.Changes().size() == 4U,
        "An unchanged target created a duplicate pending voxel change.");

    const std::vector<Asset::Voxel::VoxelDocumentChange> changes = stroke.Changes();
    Require(VoxelPencilTool::ApplyChanges({&session, &document, 0U,
                session.VoxelModelGeneration(), &history, std::nullopt,
                nullptr, nullptr}, SmartAction::Add, {6, 4, 5}, changes).Code ==
            VoxelToolResultCode::Applied && history.UndoCount() == 1U &&
            session.rebuilds_ == 1U && history.Undo(session) &&
            history.Redo(session),
        "A held Pencil stroke did not remain one atomic Undo/Redo transaction.");
}

void TestRepeatedClicksFollowCommittedFaces()
{
    Asset::Voxel::VoxelDocument document = Document({{4U, 4U, 3U, 2U}});
    TestEditSession session(document);
    VoxelEditHistory history;
    const Position normal{0, 0, 1};
    const auto applyClick = [&](const Position target)
    {
        SmartToolStroke click;
        Require(click.Begin({reinterpret_cast<std::uintptr_t>(&document),
                document.GetRevision(), session.VoxelModelGeneration(), 0U,
                [&document](const Position position)
                {
                    const auto voxel = document.GetVoxel(position);
                    return SmartToolVoxelState{voxel.has_value(),
                        voxel ? voxel->PaletteIndex : 0U};
                }}, SmartAction::Add, target, normal),
            "Unable to begin repeated Pencil click.");
        Require(PlanAndAccumulate(click, SmartAction::Add, target, normal),
            "Unable to plan repeated Pencil click.");
        const std::vector<Asset::Voxel::VoxelDocumentChange> changes = click.Changes();
        Require(changes.size() == 1U && VoxelPencilTool::ApplyChanges(
                    {&session, &document, 0U, session.VoxelModelGeneration(),
                        &history, std::nullopt, nullptr, nullptr},
                    SmartAction::Add, target, changes).Code ==
                VoxelToolResultCode::Applied,
            "A simple Pencil click did not produce one applied voxel change.");
    };

    const Editor::VoxelRay ray{{4.5F, 4.5F, 20.0F}, {0.0F, 0.0F, -1.0F}};
    const auto firstHit = RaycastVoxelDocument(document, ray);
    Require(firstHit && firstHit->AdjacentPosition == Position{4, 4, 4},
        "Initial click did not resolve its exposed document face.");
    applyClick(firstHit->AdjacentPosition);
    const auto secondHit = RaycastVoxelDocument(document, ray);
    Require(secondHit && secondHit->Coordinates == VoxelCoordinates{4U, 4U, 4U} &&
            secondHit->AdjacentPosition == Position{4, 4, 5},
        "The first committed click did not immediately expose its next face.");
    applyClick(secondHit->AdjacentPosition);
    Require(document.GetVoxel({4, 4, 4}).has_value() &&
            document.GetVoxel({4, 4, 5}).has_value() && history.UndoCount() == 2U,
        "Repeated clicks did not stack distinct document voxels atomically.");
}

void TestVirtualPaintAndRemoveState()
{
    const States source{{{4, 4, 4}, {true, 2U}}, {{5, 4, 4}, {true, 3U}}};
    SmartToolStroke paint = Begin(source, SmartAction::Paint);
    Require(PlanAndAccumulate(paint, SmartAction::Paint, {4, 4, 4}, {0, 1, 0},
                SmartToolMode::SingleVoxel, 1, 8U) &&
            PlanAndAccumulate(paint, SmartAction::Paint, {4, 4, 4}, {0, 1, 0},
                SmartToolMode::SingleVoxel, 1, 9U),
        "Paint stroke did not resolve through its virtual accumulated state.");
    const auto painted = paint.Changes();
    Require(painted.size() == 1U && painted.front().ExistedBefore &&
            painted.front().PaletteIndexBefore == 2U && painted.front().ExistsAfter &&
            painted.front().PaletteIndexAfter == 9U,
        "Paint stroke did not retain first Before and last After palette states.");

    SmartToolStroke erase = Begin(source, SmartAction::Erase);
    Require(PlanAndAccumulate(erase, SmartAction::Erase, {5, 4, 4}) &&
            PlanAndAccumulate(erase, SmartAction::Erase, {5, 4, 4}),
        "Remove stroke did not accept a revisited target.");
    const auto erased = erase.Changes();
    Require(erased.size() == 1U && erased.front().ExistedBefore &&
            erased.front().PaletteIndexBefore == 3U && !erased.front().ExistsAfter,
        "Remove stroke did not retain one original voxel change.");
}

void TestNoChangeAndOutOfBoundsPlansDoNotMutateTheStroke()
{
    const States occupied{{{4, 4, 4}, {true, 2U}}};
    SmartToolStroke add = Begin(occupied, SmartAction::Add);
    Require(PlanAndAccumulate(add, SmartAction::Add, {4, 4, 4}) &&
            !add.HasChanges(),
        "A no-change Add plan unexpectedly accumulated a document mutation.");

    const States empty;
    SmartToolStroke outside = Begin(empty, SmartAction::Add);
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session,
        Request(outside, SmartAction::Add, {16, 4, 4}));
    Require(result.Code == SmartBrushResultCode::OutOfBounds &&
            !outside.HasChanges(),
        "An out-of-bounds plan was allowed to mutate the pending stroke.");
}

void TestSafeSuspensionAndSurfaceTransition()
{
    const States empty;
    SmartToolStroke stroke = Begin(empty, SmartAction::Add, {2, 2, 2});
    const auto initial = stroke.Advance({6, 2, 2}, {0, 1, 0});
    Require(initial.size() == 4U && initial.front() == Position{3, 2, 2},
        "A same-surface segment did not interpolate its intermediate cells.");
    const auto side = stroke.Advance({6, 5, 2}, {1, 0, 0});
    Require(side == std::vector<Position>{{6, 5, 2}},
        "A surface-normal transition incorrectly bridged two surfaces.");
    stroke.Suspend();
    const auto resumed = stroke.Advance({12, 5, 2}, {1, 0, 0});
    Require(resumed == std::vector<Position>{{12, 5, 2}},
        "A stroke bridged across a temporarily invalid target.");
}

void TestBrushModesAndCancellation()
{
    const States empty;
    for (const SmartToolMode mode : {SmartToolMode::SingleVoxel,
             SmartToolMode::CubeBrush,
             SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush})
    {
        for (const int size : {3, 7})
        {
            SmartToolStroke stroke = Begin(empty);
            Require(PlanAndAccumulate(stroke, SmartAction::Add, {7, 7, 7},
                        {0, 0, 0}, mode, size),
                "A supported brush mode could not be accumulated by the stroke domain.");
            Require(stroke.HasChanges(),
                "A supported brush mode did not contribute final changes.");
        }
    }
    SmartToolStroke cancelled = Begin(empty);
    Require(PlanAndAccumulate(cancelled, SmartAction::Add, {4, 4, 4}),
        "Unable to populate cancellation stroke.");
    cancelled.Cancel();
    Require(!cancelled.IsActive() && !cancelled.HasChanges(),
        "Cancelling a stroke retained transient changes.");
}

void TestPencilSurface2DPlannerAndCache()
{
    const States empty;
    const Position target{8, 8, 8};
    const std::array<Position, 3U> normals{
        Position{1, 0, 0}, Position{0, 1, 0}, Position{0, 0, 1}};

    for (const Position normal : normals)
    {
        for (const SmartToolMode mode : {SmartToolMode::SingleVoxel,
                 SmartToolMode::CubeBrush, SmartToolMode::SphereBrush,
                 SmartToolMode::CylinderBrush})
        {
            SmartToolStroke stroke = Begin(empty, SmartAction::Add, target);
            SmartToolController controller;
            SmartToolSession session;
            const SmartToolResult preview = controller.ResolvePreview(session,
                Request(stroke, SmartAction::Add, target, normal, mode, 3, 7U,
                    SmartBrushDimension::Surface2D, SmartBrushOrientation::Z));
            Require(preview.HasPlan() &&
                    preview.Plan->BrushState().Dimension ==
                        SmartBrushDimension::Surface2D &&
                    preview.Plan->BrushState().Orientation ==
                        SmartBrushOrientation::Auto &&
                    preview.Plan->CacheKey().State.Dimension ==
                        SmartBrushDimension::Surface2D &&
                    preview.Plan->CacheKey().State.Orientation ==
                        SmartBrushOrientation::Auto,
                "The Pencil planner did not preserve 2D mode or resolve Auto orientation.");
            Require(!preview.Plan->Cells().empty(),
                "A supported 2D Pencil mode did not produce a plan.");
            if (mode == SmartToolMode::SingleVoxel)
                Require(preview.Plan->Cells().size() == 1U,
                    "A 2D Single Pencil mode did not remain one voxel.");
            for (const SmartToolPlanCell& cell : preview.Plan->Cells())
            {
                if (normal.X != 0) Require(cell.WorldPosition.X == target.X,
                    "2D Auto X did not resolve to the placement plane.");
                if (normal.Y != 0) Require(cell.WorldPosition.Y == target.Y,
                    "2D Auto Y did not resolve to the placement plane.");
                if (normal.Z != 0) Require(cell.WorldPosition.Z == target.Z,
                    "2D Auto Z did not resolve to the placement plane.");
            }
            const SmartToolResult commit = controller.ResolveCommit(session);
            Require(commit.Plan == preview.Plan && stroke.Accumulate(*commit.Plan),
                "2D Pencil commit did not consume the preview plan.");
        }
    }

    SmartToolStroke cacheStroke = Begin(empty, SmartAction::Add, target);
    const SmartToolRequest volume = Request(cacheStroke, SmartAction::Add,
        target, {0, 1, 0}, SmartToolMode::CubeBrush, 3, 7U,
        SmartBrushDimension::Volume3D);
    const SmartToolRequest surface = Request(cacheStroke, SmartAction::Add,
        target, {0, 1, 0}, SmartToolMode::CubeBrush, 3, 7U,
        SmartBrushDimension::Surface2D);
    Require(!(MakeSmartToolRequestKey(volume) == MakeSmartToolRequestKey(surface)),
        "The 2D Pencil mode was omitted from the planner cache key.");
    const SmartToolRequest surfaceWithLegacyAxis = Request(cacheStroke,
        SmartAction::Add, target, {0, 1, 0}, SmartToolMode::CubeBrush, 3,
        7U, SmartBrushDimension::Surface2D, SmartBrushOrientation::X);
    Require(MakeSmartToolRequestKey(surface) ==
            MakeSmartToolRequestKey(surfaceWithLegacyAxis),
        "The Pencil cache key did not use the resolved Auto orientation.");
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult volumePlan = controller.ResolvePreview(session, volume);
    const SmartToolResult surfacePlan = controller.ResolvePreview(session, surface);
    Require(volumePlan.HasPlan() && surfacePlan.HasPlan() &&
            volumePlan.Plan->PlanId() != surfacePlan.Plan->PlanId(),
        "Changing Pencil brush dimension reused a stale cached plan.");
}

void TestPencilSurface2DActionsStrokeUndoRedoAndDiagnostics()
{
    const Position target{6, 6, 6};
    const States paintable{{target, {true, 2U}}};
    for (const SmartAction action : {SmartAction::Add, SmartAction::Paint,
             SmartAction::Erase})
    {
        const States& source = action == SmartAction::Add ? States{} : paintable;
        SmartToolStroke stroke = Begin(source, action, target);
        Require(PlanAndAccumulate(stroke, action, target, {0, 1, 0},
                    SmartToolMode::SingleVoxel, 1, 8U,
                    SmartBrushDimension::Surface2D),
            "A supported 2D Pencil action did not accumulate its plan.");
        Require(action != SmartAction::Add || stroke.HasChanges(),
            "A 2D Pencil Add plan did not change empty source state.");
    }

    Asset::Voxel::VoxelDocument document = Document({});
    TestEditSession editSession(document);
    VoxelEditHistory history;
    SmartToolStroke stroke;
    Require(stroke.Begin({reinterpret_cast<std::uintptr_t>(&document),
                document.GetRevision(), editSession.VoxelModelGeneration(), 0U,
                [&document](const Position position)
                {
                    const auto voxel = document.GetVoxel(position);
                    return SmartToolVoxelState{voxel.has_value(),
                        voxel ? voxel->PaletteIndex : 0U};
                }}, SmartAction::Add, target, {0, 1, 0}),
        "Unable to begin the 2D Pencil transaction.");
    Require(PlanAndAccumulate(stroke, SmartAction::Add, target, {0, 1, 0},
                SmartToolMode::CubeBrush, 3, 7U,
                SmartBrushDimension::Surface2D),
        "A 2D Pencil stroke could not accumulate its first planar sample.");
    const std::vector<Position> samples = stroke.Advance({8, 6, 6}, {0, 1, 0});
    Require(samples == std::vector<Position>{{7, 6, 6}, {8, 6, 6}},
        "A 2D Pencil stroke did not interpolate its planar segment.");
    for (const Position sample : samples)
        Require(PlanAndAccumulate(stroke, SmartAction::Add, sample, {0, 1, 0},
                    SmartToolMode::CubeBrush, 3, 7U,
                    SmartBrushDimension::Surface2D),
            "A continuous 2D Pencil stroke could not accumulate an interpolated sample.");
    const auto changes = stroke.Changes();
    Require(!changes.empty() && VoxelPencilTool::ApplyChanges(
                {&editSession, &document, 0U, editSession.VoxelModelGeneration(),
                    &history, std::nullopt, nullptr, nullptr}, SmartAction::Add,
                {8, 6, 6}, changes).Code == VoxelToolResultCode::Applied &&
            history.Undo(editSession) && history.Redo(editSession),
        "A 2D Pencil stroke did not preserve atomic Undo/Redo.");

    const States occupiedSource{{{6, 6, 6}, {true, 2U}}};
    SmartToolStroke occupied = Begin(occupiedSource);
    Require(PlanAndAccumulate(occupied, SmartAction::Add, {6, 6, 6},
                {0, 1, 0}, SmartToolMode::SingleVoxel, 1, 7U,
                SmartBrushDimension::Surface2D) && !occupied.HasChanges(),
        "A 2D Pencil no-change Add plan mutated the pending stroke.");
    const States emptySource;
    SmartToolStroke outside = Begin(emptySource);
    SmartToolController planner;
    SmartToolSession planning;
    const SmartToolResult outOfBounds = planner.ResolvePreview(planning,
        Request(outside, SmartAction::Add, {16, 6, 6}, {1, 0, 0},
            SmartToolMode::CubeBrush, 3, 7U, SmartBrushDimension::Surface2D));
    Require(outOfBounds.Code == SmartBrushResultCode::OutOfBounds,
        "A fully outside 2D Pencil plan did not report its bounds diagnostic.");
}
}

int main()
{
    try
    {
        TestDeterministicInterpolation();
        TestStationarySampleAndPreviewCommitAgreement();
        TestAtomicCommitUndoRedoAndExactAggregatePreview();
        TestSingleClickIsOneAtomicHistoryOperation();
        TestCreateDeduplicationAndRevisit();
        TestVirtualPickingStacksAndContinuousExtension();
        TestRepeatedClicksFollowCommittedFaces();
        TestVirtualPaintAndRemoveState();
        TestNoChangeAndOutOfBoundsPlansDoNotMutateTheStroke();
        TestSafeSuspensionAndSurfaceTransition();
        TestBrushModesAndCancellation();
        TestPencilSurface2DPlannerAndCache();
        TestPencilSurface2DActionsStrokeUndoRedoAndDiagnostics();
        std::cout << "Smart Tool stroke tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
