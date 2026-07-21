#include "TransformGizmo/TransformGizmoManager.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] Matrix4 Projection()
{
    Matrix4 matrix = IdentityMatrix();
    matrix[0] = 0.035F;
    matrix[2] = -0.018F;
    matrix[4] = 0.008F;
    matrix[5] = 0.032F;
    matrix[6] = 0.014F;
    matrix[10] = 0.01F;
    return matrix;
}

[[nodiscard]] TransformGizmoUpdateContext VisualContext(
    const ActiveVoxelTool tool)
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 12U;
    context.SelectionDocumentGeneration = 12U;
    context.Bounds = SelectionBounds::FromCorners({2, 3, 4}, {7, 8, 9});
    context.ActiveTool = tool;
    context.CameraPosition = {0.0F, 0.0F, -20.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 1000.0F;
    context.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
    context.ViewProjection = Projection();
    return context;
}

[[nodiscard]] TransformGizmoRuntimeContext RuntimeContext(
    const ActiveVoxelTool tool)
{
    TransformGizmoRuntimeContext context;
    context.DocumentOpen = true;
    context.SessionValid = true;
    context.SelectionValid = true;
    context.OperationAvailable = true;
    context.ViewportAvailable = true;
    context.PointerOverViewport = true;
    context.ActiveTool = tool;
    context.DocumentGeneration = 12U;
    context.Bounds = SelectionBounds::FromCorners({2, 3, 4}, {7, 8, 9});
    return context;
}

struct Fixture final
{
    TransformGizmoModel Model;
    TransformGizmoInteraction Interaction;
    TransformPivotManager PivotManager;
    TransformGizmoManager Manager{Model, Interaction, PivotManager};
    TransformGizmoUpdateContext Visual = VisualContext(ActiveVoxelTool::Move);
    TransformGizmoRuntimeContext Runtime = RuntimeContext(ActiveVoxelTool::Move);
    TransformGizmoPointerInput Pointer{
        {}, Visual.Viewport, Visual.ViewProjection, std::nullopt};

    void Show(const ActiveVoxelTool tool)
    {
        Visual = VisualContext(tool);
        Runtime = RuntimeContext(tool);
        static_cast<void>(PivotManager.UpdateFromBounds(
            Visual.Bounds, {5.0F, 6.0F, 7.0F}));
        Require(PivotManager.HasValidPivot(),
            "Unable to resolve the shared pivot fixture.");
        Visual.PivotValid = PivotManager.HasValidPivot();
        Visual.PivotWorldPosition = PivotManager.GetPivot().WorldPosition;
        static_cast<void>(Manager.UpdateView(Visual));
        static_cast<void>(Manager.UpdateContext(Runtime));
    }

    [[nodiscard]] Vec2 AxisPoint(const std::size_t index) const
    {
        const auto& axis = Manager.View().Axes[index];
        const Vec3 world = axis.HasRotationRing
            ? axis.RotationRingPoints[0U] : axis.End;
        const auto projected = TransformGizmoModel::ProjectWorldToScreen(
            world, Pointer.Viewport, Pointer.ViewProjection);
        Require(projected.has_value(), "Manager fixture axis did not project.");
        return *projected;
    }

    [[nodiscard]] bool Begin(const std::size_t index = 0U)
    {
        Pointer.ScreenPosition = AxisPoint(index);
        const TransformGizmoAxis expected = Manager.View().Axes[index].Axis;
        return Manager.UpdateHover(Pointer) == expected &&
            Manager.BeginInteraction(Pointer);
    }

    void RefreshView()
    {
        Visual.InteractionState = Manager.State();
        Visual.ActiveAxis = Manager.ActiveAxis();
        static_cast<void>(Manager.UpdateView(Visual));
    }
};

void TestModesVisibilityAndImmutableView()
{
    Fixture fixture;
    Require(!fixture.Manager.IsVisible() &&
            fixture.Manager.Mode() == TransformGizmoMode::None,
        "Manager must start hidden with no active mode.");

    auto hidden = VisualContext(ActiveVoxelTool::Move);
    hidden.DocumentActive = false;
    static_cast<void>(fixture.Manager.UpdateView(hidden));
    Require(!fixture.Manager.IsVisible(),
        "Manager must hide the gizmo without a document.");
    hidden = VisualContext(ActiveVoxelTool::Move);
    hidden.SelectionEmpty = true;
    static_cast<void>(fixture.Manager.UpdateView(hidden));
    Require(!fixture.Manager.IsVisible(),
        "Manager must hide the gizmo without a selection.");

    const std::array tools{
        ActiveVoxelTool::Move,
        ActiveVoxelTool::Rotate,
        ActiveVoxelTool::Scale};
    const std::array modes{
        TransformGizmoMode::Move,
        TransformGizmoMode::Rotate,
        TransformGizmoMode::Scale};
    for (std::size_t index = 0U; index < tools.size(); ++index)
    {
        fixture.Show(tools[index]);
        Require(fixture.Manager.IsVisible() &&
                fixture.Manager.Mode() == modes[index] &&
                fixture.Manager.State() ==
                    TransformGizmoInteractionState::Idle,
            "Manager did not expose the requested valid gizmo mode.");
        TransformGizmoModel baseline;
        Require(baseline.Update(fixture.Visual) &&
                baseline.View() == fixture.Manager.View(),
            "Manager changed the immutable visual model output.");
    }
    static_assert(std::is_same_v<
        decltype(fixture.Manager.View()), const TransformGizmoView&>);
    const TransformGizmoView snapshot = fixture.Manager.View();
    auto runtime = fixture.Runtime;
    runtime.PointerOverViewport = false;
    static_cast<void>(fixture.Manager.UpdateContext(runtime));
    Require(snapshot == fixture.Manager.View(),
        "Runtime context must not mutate an already prepared view.");
}

void TestSharedPivotIsTheOnlyVisualAndInteractiveOrigin()
{
    Fixture fixture;
    const SelectionBounds first =
        SelectionBounds::FromCorners({2, 3, 4}, {7, 8, 9});
    const Vec3 modelCenter{1.0F, 2.0F, 3.0F};
    static_cast<void>(fixture.PivotManager.UpdateFromBounds(
        first, modelCenter));
    Require(fixture.PivotManager.HasValidPivot() &&
            fixture.PivotManager.GetMode() == TransformPivotMode::Center &&
            fixture.PivotManager.GetPivot().WorldPosition ==
                Vec3{4.0F, 4.0F, 4.0F},
        "The shared integration must use one valid Center pivot.");

    for (const ActiveVoxelTool tool : {
             ActiveVoxelTool::Move,
             ActiveVoxelTool::Rotate,
             ActiveVoxelTool::Scale})
    {
        fixture.Visual = VisualContext(tool);
        fixture.Visual.PivotValid = true;
        fixture.Visual.PivotWorldPosition = {999.0F, 999.0F, 999.0F};
        static_cast<void>(fixture.Manager.UpdateView(fixture.Visual));
        Require(fixture.Manager.View().Visible &&
                fixture.Manager.View().Center ==
                    fixture.PivotManager.GetPivot().WorldPosition,
            "Move, Rotate and Scale must ignore local centers and consume the manager pivot.");
    }

    const SelectionBounds second =
        SelectionBounds::FromCorners({8, 3, 4}, {11, 8, 9});
    static_cast<void>(fixture.PivotManager.UpdateFromBounds(
        second, modelCenter));
    fixture.Visual.Bounds = second;
    static_cast<void>(fixture.Manager.UpdateView(fixture.Visual));
    Require(fixture.Manager.View().Center ==
                fixture.PivotManager.GetPivot().WorldPosition,
        "A selection change must update the common gizmo pivot.");

    fixture.PivotManager.Invalidate();
    static_cast<void>(fixture.Manager.UpdateView(fixture.Visual));
    static_cast<void>(fixture.Manager.UpdateContext(fixture.Runtime));
    Require(!fixture.Manager.IsVisible() &&
            !fixture.Manager.CanBeginInteraction(),
        "An invalid pivot must hide and block the gizmo without a default origin.");
}

void TestHoverDragHelpCursorAndAxisLock()
{
    const std::array tools{
        ActiveVoxelTool::Move,
        ActiveVoxelTool::Rotate,
        ActiveVoxelTool::Scale};
    const std::array text{
        std::string_view("Move X"),
        std::string_view("Rotate X"),
        std::string_view("Scale X")};
    for (std::size_t index = 0U; index < tools.size(); ++index)
    {
        Fixture fixture;
        fixture.Show(tools[index]);
        fixture.Pointer.ScreenPosition = fixture.AxisPoint(0U);
        Require(fixture.Manager.UpdateHover(fixture.Pointer) ==
                    TransformGizmoAxis::X &&
                fixture.Manager.State() ==
                    TransformGizmoInteractionState::Hover &&
                fixture.Manager.ActiveAxis() == TransformGizmoAxis::X &&
                fixture.Manager.HelpText() == text[index] &&
                fixture.Manager.CursorRecommendation() ==
                    TransformGizmoCursorRecommendation::ResizeAll,
            "Manager did not transmit hover, help, cursor, and axis.");
        Require(fixture.Manager.BeginInteraction(fixture.Pointer) &&
                fixture.Manager.IsDragging() &&
                fixture.Manager.ActiveAxis() == TransformGizmoAxis::X &&
                fixture.Manager.State() ==
                    TransformGizmoInteractionState::Dragging &&
                fixture.Manager.HelpText().find("Release to apply") !=
                    std::string_view::npos,
            "Manager did not preserve the active axis during drag.");
        fixture.RefreshView();
        Require(fixture.Manager.View().State ==
                    TransformGizmoInteractionState::Dragging &&
                fixture.Manager.View().ActiveAxis == TransformGizmoAxis::X,
            "Manager did not transmit the interaction state to the view.");
        const TransformGizmoDragRelease release =
            fixture.Manager.EndInteraction();
        Require(release.WasDragging && release.Mode ==
                    TransformGizmoModel::ModeForTool(tools[index]) &&
                !fixture.Manager.IsDragging() &&
                fixture.Manager.State() ==
                    TransformGizmoInteractionState::Hover,
            "Manager did not finish the common interaction cleanly.");
    }
}

void TestCommonContextBlocking()
{
    Fixture fixture;
    fixture.Show(ActiveVoxelTool::Move);
    auto verifyBlocked = [&fixture](
        const TransformGizmoRuntimeContext& blocked,
        const std::string_view message)
    {
        static_cast<void>(fixture.Manager.UpdateContext(blocked));
        fixture.Pointer.ScreenPosition = fixture.AxisPoint(0U);
        Require(!fixture.Manager.CanBeginInteraction() &&
                fixture.Manager.UpdateHover(fixture.Pointer) ==
                    TransformGizmoAxis::None &&
                fixture.Manager.CursorRecommendation() ==
                    TransformGizmoCursorRecommendation::Default,
            message);
    };

    auto blocked = fixture.Runtime;
    blocked.UiCapturesPointer = true;
    verifyBlocked(blocked, "UI capture must block gizmo input.");
    blocked = fixture.Runtime;
    blocked.CameraActive = true;
    verifyBlocked(blocked, "Camera interaction must block gizmo input.");
    blocked = fixture.Runtime;
    blocked.DragDropActive = true;
    verifyBlocked(blocked, "Drag and drop must block gizmo input.");
    blocked = fixture.Runtime;
    blocked.ViewportAvailable = false;
    verifyBlocked(blocked, "Unavailable viewport must block gizmo input.");
    blocked = fixture.Runtime;
    blocked.PointerOverViewport = false;
    verifyBlocked(blocked, "Pointer outside the viewport must block begin.");
    blocked = fixture.Runtime;
    blocked.OperationAvailable = false;
    verifyBlocked(blocked, "Unavailable tool operation must block begin.");
}

void TestCommonCancellationPaths()
{
    const auto begin = [](Fixture& fixture)
    {
        fixture.Show(ActiveVoxelTool::Move);
        Require(fixture.Begin(), "Unable to begin cancellation fixture.");
    };

    Fixture explicitCancel;
    begin(explicitCancel);
    const auto cancelled = explicitCancel.Manager.CancelInteraction();
    Require(cancelled && cancelled.Mode == TransformGizmoMode::Move &&
            cancelled.Reason == TransformGizmoCancellationReason::Explicit &&
            !explicitCancel.Manager.IsDragging() &&
            !explicitCancel.Manager.CancelInteraction(),
        "Esc cancellation must be complete and idempotent.");

    Fixture toolChange;
    begin(toolChange);
    const auto changed =
        toolChange.Manager.OnToolChanged(ActiveVoxelTool::Rotate);
    Require(changed && changed.Reason ==
                TransformGizmoCancellationReason::ToolChanged,
        "Changing tool during drag must cancel the interaction.");

    const auto verifyContextCancellation = [&begin](
        auto mutate,
        const TransformGizmoCancellationReason expected,
        const std::string_view message)
    {
        Fixture fixture;
        begin(fixture);
        auto context = fixture.Runtime;
        mutate(context);
        const auto result = fixture.Manager.UpdateContext(context);
        Require(result && result.Reason == expected &&
                !fixture.Manager.IsDragging(), message);
    };
    verifyContextCancellation(
        [](auto& context) { context.SelectionValid = false; },
        TransformGizmoCancellationReason::SelectionChanged,
        "Selection loss must cancel the manager.");
    verifyContextCancellation(
        [](auto& context) { context.DocumentOpen = false; },
        TransformGizmoCancellationReason::DocumentChanged,
        "Document closure must cancel the manager.");
    verifyContextCancellation(
        [](auto& context) { ++context.DocumentGeneration; },
        TransformGizmoCancellationReason::ContextInvalid,
        "Document replacement must cancel the manager.");
    verifyContextCancellation(
        [](auto& context) { context.ViewportAvailable = false; },
        TransformGizmoCancellationReason::ViewportUnavailable,
        "Viewport loss must cancel the manager.");
    verifyContextCancellation(
        [](auto& context) { context.PreviewValid = false; },
        TransformGizmoCancellationReason::ContextInvalid,
        "Invalid preview must cancel the manager.");
    verifyContextCancellation(
        [](auto& context) { context.Closing = true; },
        TransformGizmoCancellationReason::Closing,
        "Application closure must cancel the manager.");

    Fixture pivotLoss;
    begin(pivotLoss);
    pivotLoss.PivotManager.Invalidate();
    const auto pivotCancelled =
        pivotLoss.Manager.UpdateContext(pivotLoss.Runtime);
    static_cast<void>(pivotLoss.Manager.UpdateView(pivotLoss.Visual));
    Require(pivotCancelled && pivotCancelled.Reason ==
                TransformGizmoCancellationReason::ContextInvalid &&
            !pivotLoss.Manager.IsDragging() &&
            !pivotLoss.Manager.IsVisible(),
        "Losing the shared pivot must cancel the drag and hide the gizmo.");
}

void TestRapidModeChangesAndReset()
{
    Fixture fixture;
    fixture.Show(ActiveVoxelTool::Move);
    fixture.Pointer.ScreenPosition = fixture.AxisPoint(0U);
    Require(fixture.Manager.UpdateHover(fixture.Pointer) ==
                TransformGizmoAxis::X,
        "Move hover fixture failed.");
    Require(!fixture.Manager.OnToolChanged(ActiveVoxelTool::Rotate) &&
            fixture.Manager.HoveredAxis() == TransformGizmoAxis::None,
        "Idle tool change must clear hover without fake cancellation.");
    fixture.Show(ActiveVoxelTool::Rotate);
    fixture.Show(ActiveVoxelTool::Scale);
    Require(fixture.Manager.Mode() == TransformGizmoMode::Scale,
        "Rapid Move to Rotate to Scale transition failed.");
    fixture.Manager.Reset();
    Require(!fixture.Manager.IsVisible() && !fixture.Manager.IsDragging() &&
            fixture.Manager.ActiveAxis() == TransformGizmoAxis::None &&
            fixture.Manager.HelpText().empty() &&
            fixture.Manager.LastCancellationReason() ==
                TransformGizmoCancellationReason::None,
        "Manager reset left shared interaction state behind.");
}
}

int main()
{
    try
    {
        TestModesVisibilityAndImmutableView();
        TestSharedPivotIsTheOnlyVisualAndInteractiveOrigin();
        TestHoverDragHelpCursorAndAxisLock();
        TestCommonContextBlocking();
        TestCommonCancellationPaths();
        TestRapidModeChangesAndReset();
        std::cout << "Transform Gizmo Manager tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Transform Gizmo Manager tests failed: "
                  << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
