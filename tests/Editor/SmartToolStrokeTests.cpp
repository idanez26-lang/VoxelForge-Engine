#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "SmartTools/SmartToolStroke.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
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
    const std::uint8_t palette = 7U)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = {16U, 16U, 16U};
    request.BrushRequest.State = {SmartBrushShape::Cube,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Auto,
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
    const std::uint8_t palette = 7U)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session,
        Request(stroke, action, target, normal, mode, size, palette));
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
    for (const SmartToolMode mode : {SmartToolMode::CubeBrush,
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
        TestVirtualPaintAndRemoveState();
        TestNoChangeAndOutOfBoundsPlansDoNotMutateTheStroke();
        TestSafeSuspensionAndSurfaceTransition();
        TestBrushModesAndCancellation();
        std::cout << "Smart Tool stroke tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
