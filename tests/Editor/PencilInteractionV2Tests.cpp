#include "ViewportInteractionV2/PencilCompactCommitGateway.h"
#include "ViewportInteractionV2/PencilCompactChangeResolver.h"
#include "ViewportInteractionV2/PencilInteractionHandler.h"
#include "ViewportInteractionV2/PencilViewportInteractionController.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::InteractionV2;

namespace
{
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool value, const char* const message)
{
    if (!value) throw std::runtime_error(message);
}

PencilCompactRequest Request()
{
    PencilCompactRequest request;
    request.Dimensions = {512U, 512U, 512U};
    request.Brush.Size = 1;
    request.Placement = {{32, 32, 32}, {0, 1, 0}};
    request.DocumentGeneration = 1U;
    request.DocumentRevision = 2U;
    request.PaletteIndex = 7U;
    return request;
}

PencilInteractionInput Input()
{
    return {true, true, false, false, false};
}

Asset::Voxel::VoxelDocument MakeDocument(
    const Asset::Voxel::VoxelDimensions dimensions = {8U, 8U, 8U})
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{dimensions.X, dimensions.Y, dimensions.Z}, {
        {1U, 1U, 1U, 3U}, {3U, 1U, 1U, 3U}, {5U, 1U, 1U, 3U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "pencil-interaction-v2-memory.vox");
    Require(loaded.Succeeded(), "Unable to create Pencil V2 test document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibilityModel(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel model;
    for (std::size_t index = 0U; index < document.GetPalette().size(); ++index)
    {
        const auto color = document.GetPalette()[index];
        Require(model.Palette().Set(index,
            {color.Red, color.Green, color.Blue, color.Alpha}),
            "Unable to initialize compatibility palette.");
    }
    const Asset::Voxel::VoxelSubModel* const source = document.GetModel(0U);
    Require(source != nullptr, "Test document has no submodel.");
    const auto dimensions = source->Dimensions();
    Voxel::VoxelGrid grid;
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to initialize compatibility grid.");
    source->ForEachVoxel([&grid](const Position position,
        const Asset::Voxel::Voxel voxel)
    {
        Require(grid.Set(static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to synchronize compatibility voxel.");
    });
    model.AddGrid(std::move(grid));
    return model;
}

class HistorySession final : public VoxelEditSession
{
public:
    explicit HistorySession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibilityModel(document)) {}

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 11U;
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

    std::size_t rebuilds_ = 0U;
    std::size_t completedEdits_ = 0U;

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

PencilCompactPlanPtr MakePlan(SmartToolPlanner& planner,
    const Asset::Voxel::VoxelDocument& document, const SmartAction action,
    const std::size_t palette, const Position target, const int size = 1)
{
    PencilCompactRequest request;
    request.Dimensions = *document.GetDimensions(0U);
    request.Brush.Shape = SmartBrushShape::Cube;
    request.Brush.Dimension = SmartBrushDimension::Volume3D;
    request.Brush.Size = size;
    request.Placement = {target, {0, 1, 0}};
    request.Action = action;
    request.PaletteIndex = palette;
    request.DocumentGeneration = 11U;
    request.DocumentRevision = document.GetRevision();
    const PencilCompactPlanResult result = planner.PlanPencilCompact(request);
    Require(result.HasPlan(), "Pencil V2 planner did not produce a compact plan.");
    return result.Plan;
}

PencilCompactRequest ControllerRequest(const Asset::Voxel::VoxelDocument& document,
    const Position target = {2, 2, 2})
{
    PencilCompactRequest request = Request();
    request.Dimensions = *document.GetDimensions(0U);
    request.Placement.Target = target;
    request.DocumentGeneration = 11U;
    request.DocumentRevision = document.GetRevision();
    return request;
}

PencilViewportInputFrame ControllerFrame(const std::uint64_t frame,
    const PencilCompactRequest& request, const Position target)
{
    PencilViewportInputFrame result;
    result.Frame = frame;
    result.PencilToolActive = true;
    result.Interaction = Input();
    result.Request = request;
    result.Target = target;
    return result;
}

bool IsSortedByPosition(const VoxelEditOperation& operation)
{
    for (std::size_t index = 1U; index < operation.Changes.size(); ++index)
    {
        const Position& previous = operation.Changes[index - 1U].Position;
        const Position& current = operation.Changes[index].Position;
        if (current.X < previous.X ||
            (current.X == previous.X && current.Y < previous.Y) ||
            (current.X == previous.X && current.Y == previous.Y &&
                current.Z < previous.Z)) return false;
    }
    return true;
}

void TestSession()
{
    SmartToolPlanner planner;
    PencilInteractionHandler handler;
    PencilGestureSession session;
    Require(handler.Begin(session, 1U, Request(), Input()), "begin");
    Require(session.OwnsPointer(), "owner");
    Require(handler.Drag(session, planner, {32, 32, 32}, {0, 1, 0}, Input()), "click");
    Require(session.Plans().size() == 1U, "one plan");
    Require(!handler.Drag(session, planner, {32, 32, 32}, {0, 1, 0}, Input()) &&
        session.Plans().size() == 1U, "same cell");
    Require(handler.Drag(session, planner, {36, 32, 32}, {0, 1, 0}, Input()) &&
        session.Plans().size() == 5U, "interpolate");
    for (const PencilCompactPlanPtr& plan : session.Plans())
        Require(plan && plan->PlanId() != 0U, "plan");
    handler.Escape(session);
    Require(session.Phase() == PencilGesturePhase::Cancelled &&
        session.Plans().empty(), "escape");
}

void TestBatchAndOwnership()
{
    SmartToolPlanner planner;
    PencilInteractionHandler handler;
    PencilGestureSession session;
    Require(handler.Begin(session, 1U, Request(), Input()), "begin");
    Require(!handler.Begin(session, 2U, Request(), Input()),
        "active session is never replaced");
    Require(handler.Drag(session, planner, {32, 32, 32}, {0, 1, 0}, Input()),
        "initial point");
    Require(handler.Drag(session, planner, {40, 32, 32}, {0, 1, 0}, Input()), "drag");
    const PencilInteractionMetrics& metrics = handler.Metrics();
    Require(metrics.BusinessResolveBatches == 2U,
        "one planner batch per drag update");
    Require(metrics.PlanCacheMisses == 9U && metrics.PlanBuilds == 9U,
        "all interpolated centres built once");
    Require(metrics.FootprintTranslations == 9U,
        "one translation per compact plan");
    Require(metrics.MaterializedHoverPositions == 0U,
        "hover does not materialize cells");
    Require(!handler.Drag(session, planner, {40, 32, 32}, {0, 1, 0}, Input()),
        "same terminal cell");
    Require(handler.Metrics().BusinessResolveBatches == 2U,
        "same cell has no resolve");
    handler.Escape(session);
    Require(handler.Begin(session, 3U, Request(), Input()), "new gesture");
    Require(handler.Drag(session, planner, {40, 32, 32}, {0, 1, 0}, Input()),
        "cached request");
    Require(handler.Metrics().PlanCacheHits == 1U &&
        handler.Metrics().BusinessResolveBatches == 2U,
        "same request and cell use cached plan");
}

void TestBlocking()
{
    PencilInteractionHandler handler;
    PencilGestureSession session;
    const PencilCompactRequest request = Request();
    PencilInteractionInput ui = Input();
    ui.UiCapturesPointer = true;
    Require(!handler.Begin(session, 1U, request, ui), "ui");
    PencilInteractionInput camera = Input();
    camera.CameraActive = true;
    Require(!handler.Begin(session, 1U, request, camera), "camera");
    Require(handler.Begin(session, 1U, request, Input()), "start");
    SmartToolPlanner planner;
    PencilInteractionInput lost = Input();
    lost.FocusLost = true;
    Require(!handler.Drag(session, planner, {2, 2, 2}, {0, 1, 0}, lost) &&
        session.Phase() == PencilGesturePhase::Cancelled, "focus");
}

void TestBoundedCache()
{
    SmartToolPlanner planner;
    PencilInteractionHandler handler;
    PencilGestureSession session;
    Require(handler.Begin(session, 1U, Request(), Input()), "begin");
    for (int x = 0; x < 80; ++x)
        (void)handler.Drag(session, planner, {x, 32, 32}, {0, 1, 0}, Input());
    Require(handler.CacheSize() <= 64U, "cache");
}

void TestSurfaceLockSuspensionAndDeduplication()
{
    SmartToolPlanner planner;
    PencilInteractionHandler handler;
    PencilGestureSession horizontal;
    PencilCompactRequest request = Request();
    request.Placement = {{3, 4, 5}, {0, 1, 0}};
    Require(handler.Begin(horizontal, 1U, request, Input()), "horizontal begin");
    Require(handler.Drag(horizontal, planner, {3, 4, 5}, {0, 1, 0}, Input()),
        "horizontal start");
    Require(handler.Drag(horizontal, planner, {7, 9, 5}, {0, 1, 0}, Input()),
        "horizontal locked drag");
    for (const PencilCompactPlanPtr& plan : horizontal.Plans())
        Require(plan->Placement().Target.Y == 4,
            "same normal must remain on the locked horizontal plane");
    const std::size_t horizontalPlans = horizontal.Plans().size();
    Require(!handler.Drag(horizontal, planner, {3, 4, 5}, {0, 1, 0}, Input()) &&
        horizontal.Plans().size() == horizontalPlans,
        "returning to a visited cell must not create a duplicate plan");
    Require(horizontal.Suspend() && horizontal.IsSuspended(), "suspend target loss");
    Require(handler.Drag(horizontal, planner, {12, 13, 5}, {0, 1, 0}, Input()),
        "resume from a new surface segment");
    Require(horizontal.Plans().back()->Placement().Target.Y == 4 &&
        horizontal.Plans().back()->Placement().Target.X == 12,
        "resume must not interpolate across an invalid target gap");

    PencilGestureSession vertical;
    request.Placement = {{4, 3, 5}, {1, 0, 0}};
    Require(handler.Begin(vertical, 2U, request, Input()), "vertical begin");
    Require(handler.Drag(vertical, planner, {4, 3, 5}, {1, 0, 0}, Input()),
        "vertical start");
    Require(handler.Drag(vertical, planner, {9, 7, 5}, {1, 0, 0}, Input()),
        "vertical locked drag");
    for (const PencilCompactPlanPtr& plan : vertical.Plans())
        Require(plan->Placement().Target.X == 4,
            "same normal must remain on the locked vertical plane");
    const std::size_t beforeNormalChange = vertical.Plans().size();
    Require(handler.Drag(vertical, planner, {8, 7, 6}, {0, 0, 1}, Input()) &&
        vertical.Plans().size() == beforeNormalChange + 1U,
        "a deliberate normal change starts one new surface segment");
    Require(vertical.Plans().back()->Placement().Normal == Position{0, 0, 1} &&
        vertical.Plans().back()->Placement().Target == Position{8, 7, 6},
        "new surface segment preserves its explicitly targeted face");
}

void TestCommitGateway()
{
    auto document = MakeDocument();
    HistorySession session(document);
    VoxelEditHistory history;
    SmartToolPlanner planner;

    // Add, Paint and Erase are each formed from an immutable compact plan and
    // committed as exactly one VoxelEditOperation through existing history.
    const PencilCompactPlanPtr add = MakePlan(planner, document,
        SmartAction::Add, 7U, {2, 2, 2});
    const std::vector<PencilCompactPlanPtr> addPlans{add};
    auto operation = PencilCompactCommitGateway::Build(document, 11U, addPlans);
    Require(operation && operation->Changes.size() == 1U &&
        operation->Changes.front().Position == Position{2, 2, 2},
        "Add consumes the presented compact plan exactly once");
    Require(static_cast<bool>(history.Execute(session, std::move(*operation))),
        "Add history execute");
    Require(document.GetVoxel({2, 2, 2})->PaletteIndex == 7U &&
        history.UndoCount() == 1U && session.completedEdits_ == 1U,
        "Add is one atomic transaction");
    Require(static_cast<bool>(history.Undo(session)) && !document.GetVoxel({2, 2, 2}), "Add undo");
    Require(static_cast<bool>(history.Redo(session)) && document.GetVoxel({2, 2, 2})->PaletteIndex == 7U,
        "Add redo");

    const PencilCompactPlanPtr paint = MakePlan(planner, document,
        SmartAction::Paint, 9U, {1, 1, 1});
    operation = PencilCompactCommitGateway::Build(document, 11U, {&paint, 1U});
    Require(operation && operation->Changes.size() == 1U,
        "Paint changes existing voxels only");
    Require(static_cast<bool>(history.Execute(session, std::move(*operation))) &&
        document.GetVoxel({1, 1, 1})->PaletteIndex == 9U, "Paint commit");
    Require(static_cast<bool>(history.Undo(session)) && document.GetVoxel({1, 1, 1})->PaletteIndex == 3U,
        "Paint undo");

    const PencilCompactPlanPtr erase = MakePlan(planner, document,
        SmartAction::Erase, 1U, {1, 1, 1});
    operation = PencilCompactCommitGateway::Build(document, 11U, {&erase, 1U});
    Require(operation && static_cast<bool>(history.Execute(session, std::move(*operation))) &&
        !document.GetVoxel({1, 1, 1}), "Erase commit");
    Require(static_cast<bool>(history.Undo(session)) && document.GetVoxel({1, 1, 1}), "Erase undo");

    // Plans crossing the boundary remain usable: only individual OOB cells
    // are clipped by the commit gateway.
    const PencilCompactPlanPtr clipped = MakePlan(planner, document,
        SmartAction::Add, 6U, {0, 0, 0}, 3);
    operation = PencilCompactCommitGateway::Build(document, 11U, {&clipped, 1U});
    Require(operation && !operation->Changes.empty() &&
        operation->Changes.size() < clipped->ExactVoxelCount(), "partial clipping");
    for (const VoxelChange& change : operation->Changes)
        Require(change.Position.X >= 0 && change.Position.Y >= 0 && change.Position.Z >= 0 &&
            change.Position.X < 8 && change.Position.Y < 8 && change.Position.Z < 8,
            "clipped operation has no OOB change");
}

void TestGatewayValidationAndOrdering()
{
    auto document = MakeDocument();
    SmartToolPlanner planner;
    const PencilCompactPlanPtr first = MakePlan(planner, document,
        SmartAction::Paint, 5U, {5, 1, 1});
    const PencilCompactPlanPtr second = MakePlan(planner, document,
        SmartAction::Paint, 7U, {3, 1, 1});
    const std::vector<PencilCompactPlanPtr> plans{first, second};
    auto operation = PencilCompactCommitGateway::Build(document, 11U, plans);
    Require(operation && operation->Changes.size() == 2U && IsSortedByPosition(*operation),
        "changes are deterministically sorted by world position");

    const PencilCompactPlanPtr paintFive = MakePlan(planner, document,
        SmartAction::Paint, 5U, {3, 1, 1});
    const PencilCompactPlanPtr paintSeven = MakePlan(planner, document,
        SmartAction::Paint, 7U, {3, 1, 1});
    operation = PencilCompactCommitGateway::Build(document, 11U,
        std::vector<PencilCompactPlanPtr>{paintFive, paintSeven});
    Require(operation && operation->Changes.size() == 1U &&
        operation->Changes.front().PaletteIndexAfter == 7U,
        "overlapping plans deduplicate with virtual stroke state");

    Require(!PencilCompactCommitGateway::Build(document, 12U, plans),
        "document generation mismatch rejects commit");
    const PencilCompactPlanPtr stale = MakePlan(planner, document,
        SmartAction::Paint, 5U, {3, 1, 1});
    Require(document.ReplaceVoxelColor({5, 1, 1}, 4U).Succeeded,
        "Unable to advance test document revision.");
    Require(!PencilCompactCommitGateway::Build(document, 11U, {&stale, 1U}),
        "document revision mismatch rejects commit");
}

void TestControllerHoverAndAtomicGesture()
{
    auto document = MakeDocument();
    PencilViewportInteractionController controller;
    const PencilCompactRequest request = ControllerRequest(document);

    controller.SubmitInput(ControllerFrame(1U, request, {2, 2, 2}));
    controller.Tick(&document);
    const PencilCompactPresentation& first = controller.Presentation();
    Require(!first.Active && first.Detail == PencilPreviewDetail::Exact &&
        first.Plans.size() == 1U, "hover produces one exact compact plan");
    const std::uint64_t firstRevision = first.Revision;
    const std::uint64_t firstBuilds = controller.PlanningMetrics().PlanBuilds;

    controller.SubmitInput(ControllerFrame(2U, request, {2, 2, 2}));
    controller.Tick(&document);
    Require(controller.Presentation().Revision == firstRevision &&
        controller.PlanningMetrics().PlanBuilds == firstBuilds,
        "same hover cell neither rebuilds nor rematerializes preview");

    PencilViewportInputFrame press = ControllerFrame(3U, request, {2, 2, 2});
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    controller.SubmitInput(press);
    controller.Tick(&document);
    Require(controller.OwnsPointer() && controller.Presentation().Active,
        "press grants the only pencil V2 pointer owner");
    const std::uint64_t strokeRevision = controller.Presentation().Revision;
    const std::uint64_t strokePreviewBuilds = controller.Metrics().PreviewBuilds;
    for (std::uint64_t frame = 4U; frame < 12U; ++frame)
    {
        PencilViewportInputFrame stationary = ControllerFrame(frame, request,
            {2, 2, 2});
        stationary.PrimaryHeld = true;
        controller.SubmitInput(stationary);
        controller.Tick(&document);
    }
    Require(controller.Presentation().Revision == strokeRevision &&
        controller.Metrics().PreviewBuilds == strokePreviewBuilds,
        "same stroke cell does not rebuild presentation each frame");

    PencilViewportInputFrame drag = ControllerFrame(12U, request, {4, 2, 2});
    drag.PrimaryHeld = true;
    controller.SubmitInput(drag);
    controller.Tick(&document);
    PencilViewportInputFrame release = ControllerFrame(13U, request, {4, 2, 2});
    release.PrimaryReleased = true;
    controller.SubmitInput(release);
    controller.Tick(&document);
    auto operation = controller.TakeCommit();
    Require(operation && operation->Changes.size() == 3U,
        "release creates one compact atomic operation from the stroke");
    controller.NotifyCommitApplied();
    Require(!controller.OwnsPointer() &&
        controller.Presentation().Plans.empty(), "commit leaves no zombie session");
}

void TestSharedCellResolution()
{
    auto document = MakeDocument();
    SmartToolPlanner planner;
    const PencilCompactPlanPtr paint = MakePlan(planner, document,
        SmartAction::Paint, 3U, {1, 1, 1});
    const PencilCompactCellState original{true, 3U};
    Require(paint->ResolveCell(original) == original,
        "paint no-change matches its commit decision");
    const PencilCompactPlanPtr erase = MakePlan(planner, document,
        SmartAction::Erase, 1U, {2, 2, 2});
    Require(erase->ResolveCell({false, 0U}) == PencilCompactCellState{},
        "erase absent cell matches its commit decision");
    const PencilCompactPlanPtr add = MakePlan(planner, document,
        SmartAction::Add, 7U, {2, 2, 2});
    Require(add->ResolveCell({true, 7U}) == PencilCompactCellState{true, 7U},
        "overlap no-change matches its commit decision");
}

void TestLargePaintEraseStayCompactDeferred()
{
    auto document = MakeDocument({512U, 512U, 512U});
    for (const SmartAction action : {SmartAction::Paint, SmartAction::Erase})
    {
        PencilViewportInteractionController controller;
        PencilCompactRequest request = ControllerRequest(document,
            {256, 256, 256});
        request.Brush.Shape = SmartBrushShape::Cube;
        request.Brush.Size = 256;
        request.Action = action;
        PencilViewportInputFrame input = ControllerFrame(
            action == SmartAction::Paint ? 1U : 2U, request,
            request.Placement.Target);
        controller.SubmitInput(input);
        controller.Tick(&document);
        const PencilCompactPresentation& presentation = controller.Presentation();
        Require(presentation.Detail == PencilPreviewDetail::CompactDeferred &&
            presentation.Validity == PencilPreviewValidity::Deferred &&
            presentation.ExactVoxelCount == 256U * 256U * 256U,
            "large Paint/Erase preview must stay compact and explicitly deferred");
    }
}

void TestPartiallyClippedSmallPreviewRemainsExact()
{
    auto document = MakeDocument();
    PencilViewportInteractionController controller;
    PencilCompactRequest request = ControllerRequest(document, {0, 0, 0});
    request.Brush.Shape = SmartBrushShape::Cube;
    request.Brush.Size = 3;
    controller.SubmitInput(ControllerFrame(1U, request,
        request.Placement.Target));
    controller.Tick(&document);

    const PencilCompactPresentation& presentation = controller.Presentation();
    Require(presentation.Detail == PencilPreviewDetail::Exact &&
        presentation.Validity == PencilPreviewValidity::OutOfBounds,
        "a small partially clipped footprint must keep an exact preview");
    const auto previewChanges = PencilCompactChangeResolver::Resolve(
        document, request.DocumentGeneration, presentation.Plans);
    const auto commit = PencilCompactCommitGateway::Build(
        document, request.DocumentGeneration, presentation.Plans);
    Require(previewChanges && commit &&
        previewChanges->size() == commit->Changes.size(),
        "partially clipped exact preview must match commit");
}

void TestStaleHoverRequestIsNeverPresented()
{
    auto document = MakeDocument();
    PencilViewportInteractionController controller;
    PencilCompactRequest stale = ControllerRequest(document);
    ++stale.DocumentRevision;
    controller.SubmitInput(ControllerFrame(1U, stale, {2, 2, 2}));
    controller.Tick(&document);
    Require(controller.Presentation().Plans.empty() &&
        controller.Presentation().Validity == PencilPreviewValidity::Deferred,
        "stale hover request must not produce a false preview");
}
} // namespace

int main()
{
    try
    {
        TestSession();
        TestBatchAndOwnership();
        TestBlocking();
        TestBoundedCache();
        TestSurfaceLockSuspensionAndDeduplication();
        TestCommitGateway();
        TestGatewayValidationAndOrdering();
        TestControllerHoverAndAtomicGesture();
        TestSharedCellResolution();
        TestLargePaintEraseStayCompactDeferred();
        TestPartiallyClippedSmallPreviewRemainsExact();
        TestStaleHoverRequestIsNeverPresented();
        std::cout << "Pencil Interaction V2 tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
