#include "ViewportInteractionV2/ViewportInteractionController.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;
namespace V2 = Editor::InteractionV2;

constexpr std::uint64_t Generation = 811U;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument(const std::size_t count)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {64U, 64U, 64U};
    model.Voxels.reserve(count);
    for (std::size_t index = 0U; index < count; ++index)
    {
        const std::uint32_t x = static_cast<std::uint32_t>(index % 64U);
        const std::uint32_t y =
            static_cast<std::uint32_t>((index / 64U) % 64U);
        const std::uint32_t z =
            static_cast<std::uint32_t>((index / 4096U) % 64U);
        model.Voxels.push_back({
            static_cast<std::uint8_t>(x),
            static_cast<std::uint8_t>(y),
            static_cast<std::uint8_t>(z),
            static_cast<std::uint8_t>((index % 15U) + 1U)});
    }
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "viewport-interaction-v2-memory.vox");
    Require(loaded.Succeeded(), "Unable to build V2 test document.");
    return std::move(*loaded.Document);
}

Asset::Voxel::VoxelDocument MakeDocument(
    const Asset::Voxel::VoxelDimensions dimensions,
    const std::span<const Position> positions)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {
        dimensions.X, dimensions.Y, dimensions.Z};
    model.Voxels.reserve(positions.size());
    for (const Position position : positions)
    {
        model.Voxels.push_back({
            static_cast<std::uint8_t>(position.X),
            static_cast<std::uint8_t>(position.Y),
            static_cast<std::uint8_t>(position.Z),
            1U});
    }
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "viewport-interaction-v2-scalability.vox");
    Require(loaded.Succeeded(),
        "Unable to build V2 scalability document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(
    const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "V2 compatibility model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size V2 compatibility grid.");
    model->ForEachVoxel([&grid](const Position position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to seed V2 compatibility grid.");
    });
    result.AddGrid(std::move(grid));
    return result;
}

class TestSession final : public Editor::VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibility(document)) {}

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return Generation;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    Editor::CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuildCount_;
        return Editor::CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++completedCount_; }
    void UpdateVoxelEditSavedState(bool) noexcept override {}

    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuildCount_ = 0U;
    std::size_t completedCount_ = 0U;
};

V2::ViewportInputFrame Input(
    const std::uint64_t frame, const Editor::Vec2 mouse)
{
    V2::ViewportInputFrame result;
    result.Frame = frame;
    result.MouseScreen = mouse;
    result.ViewportHovered = true;
    result.ViewportFocused = true;
    result.SelectionToolActive = true;
    result.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
    result.ViewProjection = Editor::UniformScaleMatrix(0.02F);
    result.CameraWorldPosition = {0.0F, 0.0F, 10.0F};
    result.ModelCenter = {};
    result.FramebufferScale = 1.0F;
    result.DocumentGeneration = Generation;
    return result;
}

void Submit(
    V2::ViewportInteractionController& controller,
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection,
    V2::ViewportInputFrame input)
{
    input.DocumentRevision = document.GetRevision();
    controller.SubmitInput(std::move(input));
    controller.Tick(&document, selection);
}

void ApplyHistorySelection(
    Editor::SelectionService& selection,
    const Editor::VoxelEditHistoryResult& result)
{
    Require(result.SelectionTransition != nullptr,
        "V2 operation lost its selection transition.");
    const auto& snapshot =
        result.SelectionState == Editor::VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    selection.SetDocumentGeneration(snapshot.DocumentGeneration);
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, Editor::SelectionMode::Replace));
}

void SelectFirstTwo(
    V2::ViewportInteractionController& controller,
    Asset::Voxel::VoxelDocument& document,
    Editor::SelectionService& selection)
{
    auto press = Input(1U, {499.0F, 477.0F});
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    Submit(controller, document, selection, press);
    Require(controller.Phase() == V2::InteractionPhase::Selecting,
        "Screen rectangle did not begin immediately.");
    Require(controller.Presentation().GestureOverlay2D.has_value(),
        "Screen rectangle overlay was not presented on MouseDown.");

    auto drag = Input(2U, {519.0F, 501.0F});
    drag.PrimaryHeld = true;
    Submit(controller, document, selection, drag);
    auto release = Input(3U, {519.0F, 501.0F});
    release.PrimaryReleased = true;
    Submit(controller, document, selection, release);
    Require(selection.Count() == 2U,
        "Screen footprint selection did not resolve the first two voxels.");
    Require(controller.Phase() == V2::InteractionPhase::SelectionReady,
        "Selection did not transition directly to ready state.");
    Require(controller.Presentation().SelectionBox.has_value(),
        "Stable compact selection box is missing after release.");
}

void TestScreenRectangleAndStableSession()
{
    auto document = MakeDocument(2U);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    V2::ViewportInteractionController controller;
    SelectFirstTwo(controller, document, selection);
    const std::uint64_t session = controller.Presentation().SessionId;
    Require(session != 0U, "V2 session id was not assigned.");
    Require(controller.Metrics().MaximumResolvesPerFrame <= 1U,
        "V2 resolved business state more than once in one frame.");
    Require(controller.Metrics().ProjectionBuilds == 1U,
        "Rectangle changes rebuilt the projection cache.");
    Require(controller.Presentation().SessionId == session,
        "Selection release replaced the continuous gesture session.");
}

void TestExactFootprintAndModifiers()
{
    auto document = MakeDocument(3U);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    V2::ViewportInteractionController controller;

    // This very thin rectangle intersects the projected face of voxel zero
    // while missing its projected center.
    auto press = Input(1U, {499.5F, 489.0F});
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    Submit(controller, document, selection, press);
    auto release = Input(2U, {501.0F, 511.0F});
    release.PrimaryReleased = true;
    Submit(controller, document, selection, release);
    Require(selection.Contains({0, 0, 0}),
        "Selection used a center-only test instead of footprint intersection.");

    auto addPress = Input(3U, {518.0F, 478.0F});
    addPress.PrimaryPressed = true;
    addPress.PrimaryHeld = true;
    addPress.Shift = true;
    Submit(controller, document, selection, addPress);
    auto addRelease = Input(4U, {531.0F, 501.0F});
    addRelease.PrimaryReleased = true;
    addRelease.Shift = true;
    Submit(controller, document, selection, addRelease);
    Require(selection.Count() >= 2U,
        "Shift rectangle did not add to the canonical selection.");
}

void TestMovePreviewCommitUndoRedo()
{
    auto document = MakeDocument(2U);
    document.MarkSaved();
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    V2::ViewportInteractionController controller;
    SelectFirstTwo(controller, document, selection);
    const std::uint64_t sessionId = controller.Presentation().SessionId;
    const std::uint64_t revisionBefore = document.GetRevision();
    Require(controller.Presentation().SelectionScreenBounds.has_value(),
        "Selection screen bounds are missing before Move.");
    const auto screenBounds =
        *controller.Presentation().SelectionScreenBounds;
    const Editor::Vec2 moveStart{
        (screenBounds.MinimumX + screenBounds.MaximumX) * 0.5F,
        (screenBounds.MinimumY + screenBounds.MaximumY) * 0.5F};

    auto press = Input(4U, moveStart);
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    const Editor::SelectionCenter voxelCenter =
        selection.EditableBounds().Center();
    press.PointerRay = Editor::VoxelRay{
        {voxelCenter.X, voxelCenter.Y, voxelCenter.Z + 10.0F},
        {0.0F, 0.0F, -1.0F}};
    Require(Editor::PickSelectionBoxInterior(
        selection.EditableBounds(), {}, *press.PointerRay).has_value(),
        "Test Move ray does not hit the canonical selection box.");
    Submit(controller, document, selection, press);
    Require(controller.Phase() == V2::InteractionPhase::Moving,
        "Compact selection box did not begin Move immediately.");

    auto drag = Input(5U, {moveStart.X + 20.0F, moveStart.Y});
    drag.PrimaryHeld = true;
    drag.PointerRay = Editor::VoxelRay{
        {voxelCenter.X + 2.0F, voxelCenter.Y, voxelCenter.Z + 10.0F},
        {0.0F, 0.0F, -1.0F}};
    Submit(controller, document, selection, drag);
    Require(controller.Presentation().MovePreview != nullptr,
        "Move did not present its immutable preview plan.");
    Require(document.GetRevision() == revisionBefore,
        "Move preview mutated the document before release.");

    const std::uint64_t cachedPlanId =
        controller.Presentation().PlanId;
    const std::uint64_t cachedSourceIdentity =
        controller.Presentation().MovePreview->SourceIdentity;
    const Position* const cachedSourceData =
        controller.Presentation().MovePreview->SourcePositions.data();
    const std::uint64_t cachedPresentationRevision =
        controller.Presentation().Revision;
    const std::uint64_t cachedBuilds =
        controller.Metrics().MovePlanBuilds;
    const std::uint64_t cachedProjectionBuilds =
        controller.Metrics().ProjectionBuilds;
    auto pause = Input(6U, {moveStart.X + 20.4F, moveStart.Y});
    pause.PrimaryHeld = true;
    pause.PointerRay = drag.PointerRay;
    Submit(controller, document, selection, pause);
    Require(controller.Presentation().PlanId == cachedPlanId &&
        controller.Presentation().Revision ==
            cachedPresentationRevision &&
        controller.Metrics().MovePlanBuilds == cachedBuilds &&
        controller.Metrics().MovePlanReuses >= 1U,
        "An unchanged integer delta rebuilt the Move plan or presentation.");
    Require(controller.Metrics().ProjectionBuilds ==
        cachedProjectionBuilds,
        "Move rebuilt the Screen Rectangle projection cache.");
    Require(controller.Metrics().MoveSourceCaptures == 1U &&
        controller.Metrics().MoveSourceBoundsBuilds == 1U &&
        controller.Metrics().MoveSourcePivotBuilds == 1U,
        "Move did not retain one immutable source snapshot.");

    auto differentDelta = Input(7U, {
        moveStart.X + 30.0F, moveStart.Y});
    differentDelta.PrimaryHeld = true;
    differentDelta.PointerRay = Editor::VoxelRay{
        {voxelCenter.X + 3.0F, voxelCenter.Y, voxelCenter.Z + 10.0F},
        {0.0F, 0.0F, -1.0F}};
    Submit(controller, document, selection, differentDelta);
    Require(controller.Presentation().MovePreview->SourceIdentity ==
            cachedSourceIdentity &&
        controller.Presentation().MovePreview->SourcePositions.data() ==
            cachedSourceData,
        "Delta change copied or replaced the immutable selection handle.");

    auto returnToCachedDelta = Input(8U, {
        moveStart.X + 20.0F, moveStart.Y});
    returnToCachedDelta.PrimaryHeld = true;
    returnToCachedDelta.PointerRay = drag.PointerRay;
    Submit(controller, document, selection, returnToCachedDelta);
    Require(controller.Presentation().PlanId == cachedPlanId &&
        controller.Metrics().MoveCollisionCacheHits >= 1U &&
        controller.Metrics().MoveDestinationCoordinatesMaterialized == 0U,
        "Returning to a cached delta rebuilt or materialized the Move plan.");

    auto release = Input(9U, {moveStart.X + 20.0F, moveStart.Y});
    release.PrimaryReleased = true;
    release.PointerRay = drag.PointerRay;
    Submit(controller, document, selection, release);
    auto operation = controller.TakeCommit();
    Require(operation.has_value() && !operation->Changes.empty(),
        "Move release did not produce one atomic operation.");

    TestSession session(document);
    Editor::VoxelEditHistory history;
    auto executed = history.Execute(session, std::move(*operation));
    Require(executed && history.UndoCount() == 1U &&
        session.rebuildCount_ == 1U,
        "Move was not one atomic transaction and one mesh rebuild.");
    ApplyHistorySelection(selection, executed);
    controller.NotifyCommitApplied(
        selection, Generation, document.GetRevision());
    Require(controller.Presentation().SessionId == sessionId,
        "Move validation replaced the continuous session.");
    Require(selection.Contains({2, 0, 0}) &&
        selection.Contains({3, 0, 0}),
        "Move selection transition did not retain destination voxels.");

    const auto undone = history.Undo(session);
    Require(undone && history.RedoCount() == 1U,
        "V2 Move Undo failed.");
    ApplyHistorySelection(selection, undone);
    Require(selection.Contains({0, 0, 0}) &&
        selection.Contains({1, 0, 0}),
        "Undo did not restore source selection.");
    const auto redone = history.Redo(session);
    Require(static_cast<bool>(redone), "V2 Move Redo failed.");
    ApplyHistorySelection(selection, redone);
    Require(selection.Contains({2, 0, 0}) &&
        selection.Contains({3, 0, 0}),
        "Redo did not restore destination selection.");
}

void TestCancelAndBlockedInput()
{
    auto document = MakeDocument(10U);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    V2::ViewportInteractionController controller;
    auto blocked = Input(1U, {500.0F, 500.0F});
    blocked.PrimaryPressed = true;
    blocked.UiCapturesPointer = true;
    Submit(controller, document, selection, blocked);
    Require(controller.Phase() == V2::InteractionPhase::Idle,
        "Blocked UI input began a V2 gesture.");
    auto press = Input(2U, {500.0F, 500.0F});
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    Submit(controller, document, selection, press);
    auto escape = Input(3U, {550.0F, 550.0F});
    escape.EscapePressed = true;
    Submit(controller, document, selection, escape);
    Require(controller.Phase() == V2::InteractionPhase::Idle &&
        selection.Empty() && !controller.TakeCommit(),
        "Escape did not cancel the full gesture without mutation.");
}

void TestProjectionPerformanceDatasets()
{
    constexpr std::array<std::size_t, 5U> counts{
        10U, 100U, 1'000U, 10'000U, 100'000U};
    for (const std::size_t count : counts)
    {
        auto document = MakeDocument(count);
        Editor::SelectionService selection;
        selection.SetDocumentGeneration(Generation);
        V2::ViewportInteractionController controller;
        auto press = Input(1U, {0.0F, 0.0F});
        press.PrimaryPressed = true;
        press.PrimaryHeld = true;
        Submit(controller, document, selection, press);
        for (std::uint64_t frame = 2U; frame < 12U; ++frame)
        {
            auto drag = Input(frame, {
                static_cast<float>(frame * 80U),
                static_cast<float>(frame * 70U)});
            drag.PrimaryHeld = true;
            Submit(controller, document, selection, drag);
        }
        Require(controller.Metrics().ProjectionBuilds == 1U,
            "Rectangle-only changes rebuilt a dataset projection.");
        Require(controller.Metrics().MaximumResolvesPerFrame <= 1U,
            "Dataset test exceeded one business resolve per frame.");
        Require(controller.Metrics().ProjectedVoxelCount <= count,
            "Projection cache reported an impossible voxel count.");
    }
}

void TestLargeMovePlanIsExactAndLightweight()
{
    constexpr std::size_t count = 10'000U;
    auto document = MakeDocument(count);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    std::vector<Position> positions;
    positions.reserve(count);
    document.GetModel(0U)->ForEachVoxel(
        [&positions](const Position position, const auto)
        {
            positions.push_back(position);
        });
    std::sort(positions.begin(), positions.end(),
        [](const Position left, const Position right)
        {
            if (left.X != right.X) return left.X < right.X;
            if (left.Y != right.Y) return left.Y < right.Y;
            return left.Z < right.Z;
        });
    static_cast<void>(selection.ApplySortedVolume(
        positions, Editor::SelectionBounds::FromCorners(
            {0, 0, 0}, {63, 63, 2}),
        Editor::SelectionMode::Replace));
    V2::SelectionMoveInteractionHandler handler;
    V2::ViewportInteractionMetrics metrics;
    auto selectionPlan = std::make_shared<V2::ScreenSelectionPlan>();
    selectionPlan->DocumentGeneration = Generation;
    selectionPlan->DocumentRevision = document.GetRevision();
    selectionPlan->Voxels = positions;
    selectionPlan->Bounds = selection.EditableBounds();
    const auto sourceSnapshot = handler.CaptureMoveSource(
        document, selection, Generation, selectionPlan, metrics);
    Require(sourceSnapshot != nullptr,
        "Unable to capture large Move source.");
    const auto resolved = handler.ResolveMove(
        document, sourceSnapshot, {0, 0, 1}, 91U, metrics);
    Require(resolved.Plan &&
        resolved.Plan->Validation == V2::MoveValidationState::Deferred &&
        resolved.Plan->ExactVoxelCount == count,
        "Large Move plan did not retain the exact source handle.");
    Require(metrics.MoveDestinationCoordinatesMaterialized == 0U,
        "Large Move materialized destinations during interactive drag.");
    Require(resolved.Plan->Source == sourceSnapshot &&
        metrics.MoveSourceCaptures == 1U &&
        metrics.MoveSourceBoundsBuilds == 1U &&
        metrics.MoveSourcePivotBuilds == 1U,
        "Large Move duplicated or recomputed its immutable source.");
    auto operation = handler.BuildCommit(
        document, selection, *resolved.Plan, metrics);
    std::vector<Position> expectedDestinations = positions;
    for (Position& position : expectedDestinations) ++position.Z;
    Require(operation && operation->SelectionTransition &&
        operation->SelectionTransition->After.Voxels ==
            expectedDestinations,
        "Lightweight and detailed presentation policies changed the commit.");
    Require(metrics.MoveCommitBuilds == 1U &&
        metrics.MoveCommitCoordinatesMaterialized == count,
        "Large Move did not defer transaction construction until commit.");
}

void TestMoveEscapeKeepsSelection()
{
    auto document = MakeDocument(2U);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    V2::ViewportInteractionController controller;
    SelectFirstTwo(controller, document, selection);
    const auto screenBounds =
        *controller.Presentation().SelectionScreenBounds;
    const Editor::SelectionCenter center =
        selection.EditableBounds().Center();
    auto press = Input(4U, {
        (screenBounds.MinimumX + screenBounds.MaximumX) * 0.5F,
        (screenBounds.MinimumY + screenBounds.MaximumY) * 0.5F});
    press.PrimaryPressed = true;
    press.PrimaryHeld = true;
    press.PointerRay = Editor::VoxelRay{
        {center.X, center.Y, center.Z + 10.0F},
        {0.0F, 0.0F, -1.0F}};
    Submit(controller, document, selection, press);
    auto drag = Input(5U, press.MouseScreen);
    drag.PrimaryHeld = true;
    drag.PointerRay = Editor::VoxelRay{
        {center.X + 2.0F, center.Y, center.Z + 10.0F},
        {0.0F, 0.0F, -1.0F}};
    Submit(controller, document, selection, drag);
    auto escape = Input(6U, drag.MouseScreen);
    escape.EscapePressed = true;
    Submit(controller, document, selection, escape);
    Require(controller.Phase() == V2::InteractionPhase::SelectionReady &&
        !controller.TakeCommit() &&
        selection.Contains({0, 0, 0}) &&
        selection.Contains({1, 0, 0}),
        "Escape did not cancel Move while preserving selection.");
}

void TestMoveDatasetCostsAreBounded()
{
    constexpr std::array<std::size_t, 4U> counts{
        10U, 100U, 1'000U, 10'000U};
    for (const std::size_t count : counts)
    {
        auto document = MakeDocument(count);
        Editor::SelectionService selection;
        selection.SetDocumentGeneration(Generation);
        std::vector<Position> positions;
        positions.reserve(count);
        document.GetModel(0U)->ForEachVoxel(
            [&positions](const Position position, const auto)
            {
                positions.push_back(position);
            });
        std::sort(positions.begin(), positions.end(),
            [](const Position left, const Position right)
            {
                if (left.X != right.X) return left.X < right.X;
                if (left.Y != right.Y) return left.Y < right.Y;
                return left.Z < right.Z;
            });
        const Position maximum{
            static_cast<std::int32_t>(
                std::min<std::size_t>(count, 64U) - 1U),
            static_cast<std::int32_t>(
                std::min<std::size_t>((count - 1U) / 64U, 63U)),
            static_cast<std::int32_t>((count - 1U) / 4096U)};
        static_cast<void>(selection.ApplySortedVolume(
            positions, Editor::SelectionBounds::FromCorners(
                {0, 0, 0}, maximum), Editor::SelectionMode::Replace));
        V2::SelectionMoveInteractionHandler handler;
        V2::ViewportInteractionMetrics metrics;
        auto selectionPlan = std::make_shared<V2::ScreenSelectionPlan>();
        selectionPlan->DocumentGeneration = Generation;
        selectionPlan->DocumentRevision = document.GetRevision();
        selectionPlan->Voxels = positions;
        selectionPlan->Bounds = selection.EditableBounds();
        const auto sourceSnapshot = handler.CaptureMoveSource(
            document, selection, Generation, selectionPlan, metrics);
        Require(sourceSnapshot != nullptr,
            "Unable to capture Move profiling source.");
        for (std::int32_t delta = 1; delta <= 8; ++delta)
            static_cast<void>(handler.ResolveMove(
                document, sourceSnapshot, {delta, 0, 0},
                static_cast<std::uint64_t>(delta), metrics));
        Require(metrics.MoveSourceCaptures == 1U &&
            metrics.MovePlanBuilds == 8U &&
            metrics.MoveDestinationBuilds == 0U &&
            metrics.MoveDestinationCoordinatesMaterialized == 0U &&
            metrics.MoveCompactUpdates == 8U &&
            metrics.MoveCollisionPasses <= 8U &&
            metrics.MoveCollisionCacheMisses == 8U &&
            metrics.MovePreviewBuilds == 8U,
            "Move dataset performed redundant source or plan work.");
    }
}

void TestMoveCostDependsOnSelectionNotBoxVolume()
{
    constexpr std::size_t count = 64U;
    const auto run = [](const std::int32_t side)
    {
        std::vector<Position> positions;
        positions.reserve(count);
        for (std::size_t index = 0U; index < count; ++index)
        {
            const std::int32_t coordinate = static_cast<std::int32_t>(
                index * static_cast<std::size_t>(side - 1) /
                (count - 1U));
            positions.push_back({coordinate, coordinate, coordinate});
        }
        auto document = MakeDocument({
            static_cast<std::uint32_t>(side),
            static_cast<std::uint32_t>(side),
            static_cast<std::uint32_t>(side)}, positions);
        Editor::SelectionService selection;
        selection.SetDocumentGeneration(Generation);
        const Editor::SelectionBounds bounds =
            Editor::SelectionBounds::FromCorners(
                {0, 0, 0}, {side - 1, side - 1, side - 1});
        static_cast<void>(selection.ApplySortedVolume(
            positions, bounds, Editor::SelectionMode::Replace));
        V2::SelectionMoveInteractionHandler handler;
        V2::ViewportInteractionMetrics metrics;
        auto selectionPlan = std::make_shared<V2::ScreenSelectionPlan>();
        selectionPlan->PlanId = static_cast<std::uint64_t>(side);
        selectionPlan->DocumentGeneration = Generation;
        selectionPlan->DocumentRevision = document.GetRevision();
        selectionPlan->Voxels = positions;
        selectionPlan->Bounds = bounds;
        const auto source = handler.CaptureMoveSource(
            document, selection, Generation, selectionPlan, metrics);
        Require(source != nullptr, "Sparse Move source capture failed.");
        const auto resolved = handler.ResolveMove(
            document, source, {}, 1000U, metrics);
        Require(resolved.Plan &&
            resolved.Plan->ExactVoxelCount == count,
            "Sparse Move lost the exact SelectionSet.");
        return metrics;
    };

    const V2::ViewportInteractionMetrics box64 = run(64);
    const V2::ViewportInteractionMetrics box256 = run(256);
    Require(box64.MoveSelectedCoordinatesVisited ==
            box256.MoveSelectedCoordinatesVisited &&
        box64.MoveCollisionQueries == box256.MoveCollisionQueries &&
        box64.MoveDestinationCoordinatesMaterialized == 0U &&
        box256.MoveDestinationCoordinatesMaterialized == 0U,
        "Move cost depends on empty Box volume instead of SelectionSet.");
}

void TestLargeMoveDefersInteractiveScanAndCachesDelta()
{
    constexpr std::size_t count = 100'000U;
    auto document = MakeDocument(count);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    std::vector<Position> positions;
    positions.reserve(count);
    document.GetModel(0U)->ForEachVoxel(
        [&positions](const Position position, const auto)
        {
            positions.push_back(position);
        });
    std::sort(positions.begin(), positions.end(),
        [](const Position left, const Position right)
        {
            if (left.X != right.X) return left.X < right.X;
            if (left.Y != right.Y) return left.Y < right.Y;
            return left.Z < right.Z;
        });
    const Editor::SelectionBounds bounds =
        Editor::SelectionBounds::FromCorners(
            {0, 0, 0}, {63, 63, 24});
    static_cast<void>(selection.ApplySortedVolume(
        positions, bounds, Editor::SelectionMode::Replace));
    V2::SelectionMoveInteractionHandler handler;
    V2::ViewportInteractionMetrics metrics;
    auto selectionPlan = std::make_shared<V2::ScreenSelectionPlan>();
    selectionPlan->PlanId = 2001U;
    selectionPlan->DocumentGeneration = Generation;
    selectionPlan->DocumentRevision = document.GetRevision();
    selectionPlan->Voxels = positions;
    selectionPlan->Bounds = bounds;
    const auto source = handler.CaptureMoveSource(
        document, selection, Generation, selectionPlan, metrics);
    Require(source != nullptr, "Large deferred Move source capture failed.");

    const auto first = handler.ResolveMove(
        document, source, {}, 2002U, metrics);
    const auto repeated = handler.ResolveMove(
        document, source, {}, 9999U, metrics);
    Require(first.Plan && repeated.Plan &&
        first.Plan == repeated.Plan &&
        first.Plan->PlanId == repeated.Plan->PlanId &&
        first.Plan->Validation == V2::MoveValidationState::Deferred,
        "Same large-selection delta did not reuse its immutable plan.");
    Require(metrics.MoveSelectedCoordinatesVisited == 0U &&
        metrics.MoveCollisionPasses == 0U &&
        metrics.MoveCollisionDeferred == 1U &&
        metrics.MoveCollisionCacheHits == 1U &&
        metrics.MoveDestinationCoordinatesMaterialized == 0U,
        "Large interactive Move performed an eager per-voxel scan.");
}

void TestMoveCacheIsBoundedAndCleared()
{
    auto document = MakeDocument(8U);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    std::vector<Position> positions;
    document.GetModel(0U)->ForEachVoxel(
        [&positions](const Position position, const auto)
        {
            positions.push_back(position);
        });
    std::sort(positions.begin(), positions.end(),
        [](const Position left, const Position right)
        {
            if (left.X != right.X) return left.X < right.X;
            if (left.Y != right.Y) return left.Y < right.Y;
            return left.Z < right.Z;
        });
    const Editor::SelectionBounds bounds =
        Editor::SelectionBounds::FromCorners({0, 0, 0}, {7, 0, 0});
    static_cast<void>(selection.ApplySortedVolume(
        positions, bounds, Editor::SelectionMode::Replace));
    V2::SelectionMoveInteractionHandler handler;
    V2::ViewportInteractionMetrics metrics;
    auto selectionPlan = std::make_shared<V2::ScreenSelectionPlan>();
    selectionPlan->PlanId = 3001U;
    selectionPlan->DocumentGeneration = Generation;
    selectionPlan->DocumentRevision = document.GetRevision();
    selectionPlan->Voxels = positions;
    selectionPlan->Bounds = bounds;
    const auto source = handler.CaptureMoveSource(
        document, selection, Generation, selectionPlan, metrics);
    Require(source != nullptr, "Bounded Move cache source capture failed.");

    for (std::int32_t delta = -96; delta <= 96; ++delta)
    {
        static_cast<void>(handler.ResolveMove(
            document, source, {delta, 0, 0},
            static_cast<std::uint64_t>(4000 + delta + 96), metrics));
        Require(handler.MoveCacheEntryCount() <= 64U,
            "Move cache exceeded its explicit interaction budget.");
    }
    Require(handler.MoveCacheEntryCount() > 0U,
        "Move cache did not retain recently visited deltas.");
    handler.ClearMoveCache();
    Require(handler.MoveCacheEntryCount() == 0U,
        "Move cache survived explicit gesture cancellation cleanup.");
}

}

int main()
{
    try
    {
        TestScreenRectangleAndStableSession();
        TestExactFootprintAndModifiers();
        TestMovePreviewCommitUndoRedo();
        TestCancelAndBlockedInput();
        TestProjectionPerformanceDatasets();
        TestLargeMovePlanIsExactAndLightweight();
        TestMoveEscapeKeepsSelection();
        TestMoveDatasetCostsAreBounded();
        TestMoveCostDependsOnSelectionNotBoxVolume();
        TestLargeMoveDefersInteractiveScanAndCachesDelta();
        TestMoveCacheIsBoundedAndCleared();
        std::cout << "Viewport Interaction V2 tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
