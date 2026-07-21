#include "TransformGizmo/TransformGizmoInteraction.h"
#include "TransformGizmo/GizmoStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;
using VoxelForge::Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] TransformGizmoView MakeView()
{
    TransformGizmoView view;
    view.Visible = true;
    view.Mode = TransformGizmoMode::Move;
    view.State = TransformGizmoInteractionState::Idle;
    view.Center = {};
    view.AxisLength = 10.0F;
    view.AxisThickness = 0.25F;
    view.CenterRadius = 0.5F;
    view.Axes = {{
        {TransformGizmoAxis::X, {}, {10.0F, 0.0F, 0.0F}, {1, 0, 0, 1}, 0.25F},
        {TransformGizmoAxis::Y, {}, {0.0F, 10.0F, 0.0F}, {0, 1, 0, 1}, 0.25F},
        {TransformGizmoAxis::Z, {}, {0.0F, 0.0F, 10.0F}, {0, 0, 1, 1}, 0.25F}}};
    return view;
}

[[nodiscard]] Matrix4 MakeProjection()
{
    Matrix4 matrix = IdentityMatrix();
    matrix[0] = 0.02F;
    matrix[2] = -0.014F;
    matrix[5] = 0.02F;
    matrix[6] = 0.014F;
    matrix[10] = 0.01F;
    return matrix;
}

[[nodiscard]] Matrix4 MakePerspectiveProjection(
    const float cameraDepth,
    const float viewportWidth,
    const float viewportHeight,
    const float verticalFovDegrees = 45.0F)
{
    const float yScale = 1.0F / std::tan(
        DegreesToRadians(verticalFovDegrees) * 0.5F);
    const float xScale = yScale / (viewportWidth / viewportHeight);
    return {
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F, cameraDepth};
}

[[nodiscard]] TransformGizmoView MakeHybridView(
    const float cameraDepth,
    const float viewportHeight,
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}))
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 9U;
    context.SelectionDocumentGeneration = 9U;
    context.Bounds = bounds;
    context.PivotValid = true;
    context.PivotWorldPosition = {};
    context.ActiveTool = ActiveVoxelTool::Move;
    context.CameraPosition = {0.0F, 0.0F, -cameraDepth};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = viewportHeight;
    context.Viewport = {0.0F, 0.0F,
        viewportHeight * (16.0F / 9.0F), viewportHeight};
    context.ViewProjection = MakePerspectiveProjection(
        cameraDepth, context.Viewport.Width, context.Viewport.Height);
    TransformGizmoModel model;
    Require(model.Update(context) && model.View().Visible,
        "Unable to construct the hybrid Move gizmo fixture.");
    return model.View();
}

[[nodiscard]] Vec2 PointerFor(const TransformGizmoAxis axis)
{
    if (axis == TransformGizmoAxis::X) return {575.0F, 500.0F};
    if (axis == TransformGizmoAxis::Y) return {500.0F, 425.0F};
    return {447.5F, 447.5F};
}

[[nodiscard]] Vec2 ScreenDirection(const TransformGizmoAxis axis)
{
    if (axis == TransformGizmoAxis::X) return {1.0F, 0.0F};
    if (axis == TransformGizmoAxis::Y) return {0.0F, -1.0F};
    constexpr float inverseRootTwo = 0.70710678118F;
    return {-inverseRootTwo, -inverseRootTwo};
}

[[nodiscard]] TransformGizmoPointerInput MakeInput(
    const TransformGizmoAxis axis)
{
    TransformGizmoPointerInput input;
    input.ScreenPosition = PointerFor(axis);
    input.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
    input.ViewProjection = MakeProjection();
    return input;
}

[[nodiscard]] bool Begin(
    TransformGizmoInteraction& interaction,
    const TransformGizmoAxis axis,
    TransformGizmoPointerInput& input)
{
    input = MakeInput(axis);
    return interaction.UpdateHover(MakeView(), input) == axis &&
        interaction.BeginDrag(MakeView(), axis, input, 9U,
            SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2}));
}

void TestPickingAndPriority()
{
    TransformGizmoInteraction interaction;
    auto view = MakeView();
    for (const TransformGizmoAxis axis : {
             TransformGizmoAxis::X,
             TransformGizmoAxis::Y,
             TransformGizmoAxis::Z})
    {
        auto input = MakeInput(axis);
        Require(interaction.UpdateHover(view, input) == axis,
            "Each Move axis must be pickable in screen space.");
        Require(interaction.State() == TransformGizmoInteractionState::Hover,
            "A picked axis must produce the Hover state.");
    }

    auto input = MakeInput(TransformGizmoAxis::X);
    input.ScreenPosition = {500.0F, 520.0F};
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::None &&
            interaction.State() == TransformGizmoInteractionState::Idle,
        "Picking outside the 8 px tolerance must miss.");
    input.ScreenPosition = {500.0F, 507.9F};
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::X,
        "Picking inside the tolerance must hit.");
    input.ScreenPosition = {500.0F, 500.0F};
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::X,
        "Exact ties must resolve deterministically in X/Y/Z order.");

    view.Mode = TransformGizmoMode::Rotate;
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::None,
        "Rotate visuals must remain non-interactive in Move Gizmo v1.");
    view.Mode = TransformGizmoMode::Scale;
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::None,
        "Scale visuals must remain non-interactive in Move Gizmo v1.");
    view = MakeView();
    view.Visible = false;
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::None,
        "A hidden gizmo must never capture input.");
    input.Viewport.Width = 0.0F;
    Require(interaction.UpdateHover(MakeView(), input) == TransformGizmoAxis::None,
        "A degenerate viewport must be rejected safely.");
}

void TestPickingMatchesHybridRenderedSegmentsAtEveryDepth()
{
    for (const auto [cameraDepth, viewportWidth, viewportHeight] :
         {std::array<float, 3U>{0.05F, 640.0F, 360.0F},
          std::array<float, 3U>{1.0F, 1280.0F, 720.0F},
          std::array<float, 3U>{250.0F, 2560.0F, 1440.0F}})
    {
        TransformGizmoView view = MakeHybridView(
            cameraDepth, viewportHeight);
        const float invalid = std::numeric_limits<float>::quiet_NaN();
        view.Axes[1].Start = {invalid, invalid, invalid};
        view.Axes[1].End = {invalid, invalid, invalid};
        view.Axes[2].Start = {invalid, invalid, invalid};
        view.Axes[2].End = {invalid, invalid, invalid};
        TransformGizmoPointerInput input;
        input.Viewport = {0.0F, 0.0F, viewportWidth, viewportHeight};
        input.ViewProjection = MakePerspectiveProjection(
            cameraDepth, viewportWidth, viewportHeight);
        input.ScreenPosition = {
            viewportWidth * 0.5F +
                view.Axes[0].ProjectedLengthPixels * 0.5F,
            viewportHeight * 0.5F};
        TransformGizmoInteraction interaction;
        Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::X,
            "Picking must hit the exact rendered X segment at near, normal, and far depth.");
        if (view.Axes[0].ProjectedLengthPixels > 20.0F)
        {
            input.ScreenPosition.Y +=
                TransformGizmoInteraction::PickTolerancePixels - 0.1F;
            Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::X,
                "The minimum pixel picking tolerance must remain usable across depth and resolution.");
            input.ScreenPosition.Y += 1.0F;
            Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::None,
                "Picking must not create a large invisible region around a normal shaft.");
        }
    }

    const SelectionBounds large =
        SelectionBounds::FromCorners({0, 0, 0}, {63, 63, 63});
    const TransformGizmoView small = MakeHybridView(20.0F, 720.0F);
    const TransformGizmoView largeView =
        MakeHybridView(20.0F, 720.0F, large);
    Require(largeView.AxisLength > small.AxisLength,
        "Visual geometry must scale with the selection while picking remains screen based.");

    TransformGizmoUpdateContext previewContext;
    previewContext.DocumentActive = true;
    previewContext.SelectionEmpty = false;
    previewContext.ActiveDocumentGeneration = 9U;
    previewContext.SelectionDocumentGeneration = 9U;
    previewContext.Bounds =
        SelectionBounds::FromCorners({10, 0, 0}, {10, 0, 0});
    previewContext.PivotValid = true;
    previewContext.PivotWorldPosition = {10.0F, 0.0F, 0.0F};
    previewContext.ActiveTool = ActiveVoxelTool::Move;
    previewContext.CameraPosition = {0.0F, 0.0F, -20.0F};
    previewContext.CameraForward = {0.0F, 0.0F, 1.0F};
    previewContext.ViewportHeightPixels = 720.0F;
    TransformGizmoModel previewModel;
    Require(previewModel.Update(previewContext) &&
            previewModel.View().Center == Vec3{10.0F, 0.0F, 0.0F} &&
            std::abs(previewModel.View().AxisLength - small.AxisLength) < 0.0001F,
        "A Move preview must relocate same-sized bounds without changing gizmo size.");

    TransformGizmoPointerInput faceOn;
    faceOn.Viewport = {0.0F, 0.0F, 1280.0F, 720.0F};
    faceOn.ViewProjection = MakePerspectiveProjection(20.0F, 1280.0F, 720.0F);
    faceOn.ScreenPosition = {640.0F, 360.0F};
    TransformGizmoInteraction faceOnInteraction;
    Require(faceOnInteraction.UpdateHover(small, faceOn) == TransformGizmoAxis::X,
        "A camera-aligned collapsed axis must remain finite and resolve ties deterministically.");
}

void TestArrowHeadsAndPickingUseTheFinalProjection()
{
    for (const auto [cameraDepth, viewportWidth, viewportHeight] :
         {std::array<float, 3U>{0.1F, 640.0F, 360.0F},
          std::array<float, 3U>{20.0F, 1280.0F, 720.0F},
          std::array<float, 3U>{500.0F, 2560.0F, 1440.0F}})
    {
        const TransformGizmoView view = MakeHybridView(
            cameraDepth, viewportHeight);
        const TransformGizmoAxisView& x = view.Axes[0];
        const TransformGizmoAxisView& y = view.Axes[1];
        const TransformGizmoAxisView& z = view.Axes[2];
        Require(x.HasArrowHead && y.HasArrowHead && z.HasArrowHead &&
                x.Axis == TransformGizmoAxis::X &&
                y.Axis == TransformGizmoAxis::Y &&
                z.Axis == TransformGizmoAxis::Z &&
                x.ArrowBaseCenter.X < x.End.X &&
                x.ArrowBaseCenter.Y == x.End.Y &&
                x.ArrowBaseCenter.Z == x.End.Z &&
                y.ArrowBaseCenter.X == y.End.X &&
                y.ArrowBaseCenter.Y < y.End.Y &&
                y.ArrowBaseCenter.Z == y.End.Z &&
                z.ArrowBaseCenter.X == z.End.X &&
                z.ArrowBaseCenter.Y == z.End.Y &&
                z.ArrowBaseCenter.Z < z.End.Z,
            "Move must prepare one correctly oriented positive arrow per axis.");
        TransformGizmoPointerInput input;
        input.Viewport = {0.0F, 0.0F, viewportWidth, viewportHeight};
        input.ViewProjection = MakePerspectiveProjection(
            cameraDepth, viewportWidth, viewportHeight);
        const auto tip = TransformGizmoModel::ProjectWorldToScreen(
            x.End, input.Viewport, input.ViewProjection);
        Require(tip.has_value(), "The arrow tip must project to screen space.");
        input.ScreenPosition = *tip;
        TransformGizmoInteraction interaction;
        Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::X,
            "The visible arrow tip must be pickable at near, medium and far zoom.");
    }

    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 9U;
    context.SelectionDocumentGeneration = 9U;
    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0});
    context.PivotValid = true;
    context.PivotWorldPosition = {};
    context.ActiveTool = ActiveVoxelTool::Move;
    context.CameraPosition = {0.0F, 0.0F, -20.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 720.0F;
    context.Viewport = {0.0F, 0.0F, 1280.0F, 720.0F};
    context.ViewProjection = MakePerspectiveProjection(
        20.0F, 1280.0F, 720.0F);
    context.InteractionState = TransformGizmoInteractionState::Hover;
    context.ActiveAxis = TransformGizmoAxis::X;
    TransformGizmoModel model;
    Require(model.Update(context), "Unable to construct hovered arrows.");
    const TransformGizmoView hovered = model.View();
    context.InteractionState = TransformGizmoInteractionState::Dragging;
    Require(model.Update(context), "Unable to construct dragged arrows.");
    const TransformGizmoView dragged = model.View();
    Require(dragged.Axes[0].HasArrowHead &&
            dragged.Axes[0].Color[0] > hovered.Axes[0].Color[0] &&
            dragged.Axes[0].Thickness < hovered.Axes[0].Thickness &&
            dragged.Axes[1].Color[1] < hovered.Axes[1].Color[1],
        "The locked segment and its arrow must stay highlighted together while other axes dim.");
}

void TestShaftAndArrowTipPickingStaySynchronized()
{
    TransformGizmoView view = MakeView();
    constexpr float base = 8.5F;
    constexpr float halfWidth = 0.5F;
    view.Axes[0].HasArrowHead = true;
    view.Axes[0].ArrowBaseCenter = {base, 0.0F, 0.0F};
    view.Axes[0].ArrowBaseCorners = {{{base, -halfWidth, -halfWidth},
        {base, halfWidth, -halfWidth}, {base, halfWidth, halfWidth},
        {base, -halfWidth, halfWidth}}};
    view.Axes[1].HasArrowHead = true;
    view.Axes[1].ArrowBaseCenter = {0.0F, base, 0.0F};
    view.Axes[1].ArrowBaseCorners = {{{-halfWidth, base, -halfWidth},
        {halfWidth, base, -halfWidth}, {halfWidth, base, halfWidth},
        {-halfWidth, base, halfWidth}}};
    view.Axes[2].HasArrowHead = true;
    view.Axes[2].ArrowBaseCenter = {0.0F, 0.0F, base};
    view.Axes[2].ArrowBaseCorners = {{{-halfWidth, -halfWidth, base},
        {halfWidth, -halfWidth, base}, {halfWidth, halfWidth, base},
        {-halfWidth, halfWidth, base}}};

    TransformGizmoPointerInput input;
    input.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
    input.ViewProjection = MakeProjection();
    TransformGizmoInteraction interaction;
    for (std::size_t index = 0U; index < view.Axes.size(); ++index)
    {
        const TransformGizmoAxisView& axis = view.Axes[index];
        const auto shaft = TransformGizmoModel::ProjectWorldToScreen(
            (axis.Start + axis.ArrowBaseCenter) * 0.5F,
            input.Viewport, input.ViewProjection);
        const auto tip = TransformGizmoModel::ProjectWorldToScreen(
            axis.End, input.Viewport, input.ViewProjection);
        Require(shaft.has_value() && tip.has_value(),
            "Every professional Move arrow must project fully.");
        input.ScreenPosition = *shaft;
        Require(interaction.UpdateHover(view, input) == axis.Axis,
            "Each rendered shaft must be pickable.");
        input.ScreenPosition = *tip;
        Require(interaction.UpdateHover(view, input) == axis.Axis,
            "Each rendered arrow tip must be pickable without a ghost region.");
    }

    view.Mode = TransformGizmoMode::Rotate;
    for (TransformGizmoAxisView& axis : view.Axes) axis.HasArrowHead = false;
    Require(std::ranges::none_of(view.Axes,
                [](const TransformGizmoAxisView& axis)
                {
                    return axis.HasArrowHead;
                }),
        "Rotate must not inherit Move arrow heads.");
    view.Mode = TransformGizmoMode::Scale;
    Require(std::ranges::none_of(view.Axes,
                [](const TransformGizmoAxisView& axis)
                {
                    return axis.HasArrowHead;
                }),
        "Scale must not inherit Move arrow heads.");
}

void TestAxisLockedIntegerDragging()
{
    for (const TransformGizmoAxis axis : {
             TransformGizmoAxis::X,
             TransformGizmoAxis::Y,
             TransformGizmoAxis::Z})
    {
        TransformGizmoInteraction interaction;
        TransformGizmoPointerInput input;
        Require(Begin(interaction, axis, input),
            "A hovered Move axis must begin dragging.");
        Require(interaction.IsDragging() && interaction.LockedAxis() == axis &&
                interaction.State() == TransformGizmoInteractionState::Dragging,
            "BeginDrag must lock exactly one axis.");
        const Vec2 direction = ScreenDirection(axis);
        input.ScreenPosition = {
            input.ScreenPosition.X + direction.X * 26.0F,
            input.ScreenPosition.Y + direction.Y * 26.0F};
        Require(interaction.UpdateDrag(input),
            "A grid-sized pointer change must update the preview delta.");
        const VoxelPosition positive = interaction.Delta();
        Require((axis == TransformGizmoAxis::X && positive == VoxelPosition{3, 0, 0}) ||
                (axis == TransformGizmoAxis::Y && positive == VoxelPosition{0, 3, 0}) ||
                (axis == TransformGizmoAxis::Z && positive == VoxelPosition{0, 0, 3}),
            "Drag deltas must be integer, mono-axis and positive.");
        Require(!interaction.UpdateDrag(input),
            "An unchanged integer delta must not request a preview rebuild.");

        input.ScreenPosition = {
            PointerFor(axis).X - direction.X * 26.0F,
            PointerFor(axis).Y - direction.Y * 26.0F};
        Require(interaction.UpdateDrag(input),
            "Negative dragging must update the preview delta.");
        const VoxelPosition negative = interaction.Delta();
        Require((axis == TransformGizmoAxis::X && negative == VoxelPosition{-3, 0, 0}) ||
                (axis == TransformGizmoAxis::Y && negative == VoxelPosition{0, -3, 0}) ||
                (axis == TransformGizmoAxis::Z && negative == VoxelPosition{0, 0, -3}),
            "Drag deltas must remain locked during negative movement.");
        const TransformGizmoDragRelease release = interaction.EndDrag();
        Require(release.WasDragging && release.Axis == axis &&
                release.Delta == negative && !interaction.IsDragging(),
            "MouseUp must expose one final delta and release capture.");
    }
}

void TestRoundingRayGeometryAndParallelFallback()
{
    TransformGizmoInteraction interaction;
    TransformGizmoPointerInput input;
    Require(Begin(interaction, TransformGizmoAxis::X, input),
        "Unable to begin rounding fixture.");
    input.ScreenPosition.X += 4.9F;
    Require(!interaction.UpdateDrag(input) && interaction.Delta() == VoxelPosition{},
        "Movement below half a voxel must remain zero.");
    input.ScreenPosition.X += 0.2F;
    Require(interaction.UpdateDrag(input) &&
            interaction.Delta() == VoxelPosition{1, 0, 0},
        "Nearest-integer rounding must switch at half a voxel.");
    interaction.Reset();

    input = MakeInput(TransformGizmoAxis::X);
    input.Ray = VoxelRay{{0.0F, 0.0F, -10.0F}, {0.0F, 0.0F, 1.0F}};
    Require(interaction.UpdateHover(MakeView(), input) == TransformGizmoAxis::X &&
            interaction.BeginDrag(MakeView(), TransformGizmoAxis::X, input, 9U,
                SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2})),
        "Unable to begin ray-constrained fixture.");
    input.Ray = VoxelRay{{0.0F, 0.0F, -10.0F},
        Normalize(Vec3{3.0F, 0.0F, 10.0F})};
    Require(interaction.UpdateDrag(input) &&
            interaction.Delta() == VoxelPosition{3, 0, 0},
        "Closest ray/axis geometry must recover the exact X displacement.");
    interaction.Reset();

    input = MakeInput(TransformGizmoAxis::X);
    input.Ray = VoxelRay{{-10.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    Require(interaction.UpdateHover(MakeView(), input) == TransformGizmoAxis::X &&
            interaction.BeginDrag(MakeView(), TransformGizmoAxis::X, input, 9U,
                SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2})),
        "A camera-parallel axis must still begin safely.");
    input.ScreenPosition.X += 20.0F;
    Require(interaction.UpdateDrag(input) &&
            interaction.Delta() == VoxelPosition{2, 0, 0},
        "A ray parallel to the axis must use the stable screen fallback.");
    input.ScreenPosition.X = std::numeric_limits<float>::quiet_NaN();
    Require(!interaction.UpdateDrag(input),
        "Non-finite pointer input must never create NaN or mutate the delta.");
}

void TestInvalidationCancellationAndCaptureLifetime()
{
    TransformGizmoInteraction interaction;
    TransformGizmoPointerInput input;
    Require(Begin(interaction, TransformGizmoAxis::Y, input),
        "Unable to begin cancellation fixture.");
    Require(interaction.UpdateHover(MakeView(), MakeInput(TransformGizmoAxis::X)) ==
            TransformGizmoAxis::Y,
        "Hover updates during capture must preserve the locked axis.");
    Require(interaction.Validate(9U,
            SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2}), true),
        "An unchanged document and selection must preserve capture.");
    Require(!interaction.Validate(10U,
            SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2}), true) &&
            !interaction.IsDragging(),
        "A document generation change must cancel capture.");

    Require(Begin(interaction, TransformGizmoAxis::Y, input),
        "Unable to restart after document invalidation.");
    Require(!interaction.Validate(9U,
            SelectionBounds::FromCorners({1, 1, 1}, {3, 2, 2}), true),
        "A selection bounds change must cancel capture.");
    Require(Begin(interaction, TransformGizmoAxis::Y, input),
        "Unable to restart after selection invalidation.");
    Require(!interaction.Validate(9U,
            SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2}), false),
        "Changing tools must cancel capture.");
    Require(Begin(interaction, TransformGizmoAxis::Y, input),
        "Unable to restart before Esc cancellation.");
    Require(interaction.Cancel() && !interaction.IsDragging() &&
            interaction.LockedAxis() == TransformGizmoAxis::None &&
            interaction.Delta() == VoxelPosition{},
        "Esc-style cancellation must purge all transient state.");
    Require(!interaction.Cancel(),
        "Cancelling an already idle interaction must be a no-op.");

    auto view = MakeView();
    input = MakeInput(TransformGizmoAxis::X);
    Require(interaction.UpdateHover(view, input) == TransformGizmoAxis::X,
        "Hover fixture setup failed.");
    Require(!interaction.BeginDrag(view, TransformGizmoAxis::Y, input, 9U,
            SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2})),
        "A non-hovered axis must not steal capture.");
    Require(!interaction.BeginDrag(view, TransformGizmoAxis::X, input, 0U,
            SelectionBounds::FromCorners({1, 1, 1}, {2, 2, 2})),
        "A missing document generation must reject capture.");
    Require(!interaction.BeginDrag(view, TransformGizmoAxis::X, input, 9U, {}),
        "Invalid selection bounds must reject capture.");
}

void TestVisualStateIsPreparedOutsideRenderer()
{
    TransformGizmoModel model;
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 9U;
    context.SelectionDocumentGeneration = 9U;
    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {2, 2, 2});
    context.PivotValid = true;
    context.PivotWorldPosition = {1.5F, 1.5F, 1.5F};
    context.ActiveTool = ActiveVoxelTool::Move;
    context.CameraPosition = {0.0F, 0.0F, -10.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 800.0F;
    context.InteractionState = TransformGizmoInteractionState::Hover;
    context.ActiveAxis = TransformGizmoAxis::X;
    Require(model.Update(context), "Hover view was not rebuilt.");
    const auto hover = model.View();
    Require(hover.State == TransformGizmoInteractionState::Hover &&
            hover.ActiveAxis == TransformGizmoAxis::X &&
            hover.Axes[0].Thickness > hover.Axes[1].Thickness,
        "The model must prepare a thicker active axis for the renderer.");

    context.InteractionState = TransformGizmoInteractionState::Dragging;
    Require(model.Update(context), "Dragging view was not rebuilt.");
    const auto drag = model.View();
    Require(drag.State == TransformGizmoInteractionState::Dragging &&
            drag.Axes[0].Color[0] > drag.Axes[1].Color[0] &&
            drag.Axes[2].Color[2] < hover.Axes[2].Color[2],
        "Dragging must emphasize only the locked axis and dim the others.");
    context.ActiveTool = ActiveVoxelTool::Rotate;
    Require(model.Update(context) &&
            model.View().State == TransformGizmoInteractionState::Dragging &&
            model.View().ActiveAxis == TransformGizmoAxis::X &&
            model.View().Axes[0].HasRotationRing,
        "Rotate must now consume the shared gizmo interaction state.");
}

void TestProfessionalMoveGizmoStyleAndContextHelp()
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 9U;
    context.SelectionDocumentGeneration = 9U;
    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {4, 2, 1});
    context.PivotValid = true;
    context.PivotWorldPosition = {};
    context.ActiveTool = ActiveVoxelTool::Move;
    context.CameraPosition = {0.0F, 0.0F, -20.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 720.0F;
    context.Viewport = {0.0F, 0.0F, 1280.0F, 720.0F};
    context.ViewProjection = MakePerspectiveProjection(
        20.0F, context.Viewport.Width, context.Viewport.Height);

    TransformGizmoModel model;
    Require(model.Update(context), "Unable to build the normal Move style.");
    const TransformGizmoView normal = model.View();
    constexpr std::array<std::array<float, 4U>, 3U> colors{
        GizmoStyle::AxisColorX,
        GizmoStyle::AxisColorY,
        GizmoStyle::AxisColorZ};
    Require(normal.Visible && normal.CenterRadius > 0.0F &&
            normal.Axes[0].Color[0] > normal.Axes[0].Color[1] &&
            normal.Axes[1].Color[1] > normal.Axes[1].Color[0] &&
            normal.Axes[2].Color[2] > normal.Axes[2].Color[0],
        "The normal style must keep a discreet center and canonical RGB axes.");
    for (std::size_t index = 0U; index < normal.Axes.size(); ++index)
    {
        const TransformGizmoAxisView& axis = normal.Axes[index];
        const float worldLength = Length(axis.End - axis.Start);
        const float thicknessPixels = axis.Thickness *
            axis.ProjectedLengthPixels / worldLength;
        Require(axis.HasArrowHead && axis.Thickness > 0.0F &&
                axis.ArrowLength > 0.0F && axis.ArrowWidth > 0.0F &&
                axis.ArrowWidth < axis.ArrowLength &&
                (axis.CameraFacing || std::abs(thicknessPixels -
                    GizmoStyle::MoveAxisIdleThicknessPixels) < 0.01F) &&
                std::abs(axis.Color[0] -
                    colors[index][0] * GizmoStyle::IdleIntensity) < 0.001F &&
                std::abs(axis.Color[1] -
                    colors[index][1] * GizmoStyle::IdleIntensity) < 0.001F &&
                std::abs(axis.Color[2] -
                    colors[index][2] * GizmoStyle::IdleIntensity) < 0.001F,
            "Move must expose three thin shafts with compact arrow heads.");
    }
    Require(normal.CenterRadius < normal.AxisThickness &&
            GizmoStyle::MoveCenterDiameterPixels == 3.0F &&
            GizmoStyle::AxisColorX == GizmoStyle::RotateXColor &&
            GizmoStyle::AxisColorY == GizmoStyle::RotateYColor &&
            GizmoStyle::AxisColorZ == GizmoStyle::RotateZColor &&
            GizmoStyle::MoveAxisIdleThicknessPixels >= 1.65F &&
            GizmoStyle::MoveAxisIdleThicknessPixels <= 1.85F &&
            GizmoStyle::MoveAxisHoverThicknessPixels >
                GizmoStyle::MoveAxisIdleThicknessPixels &&
            GizmoStyle::MoveAxisHoverThicknessPixels <= 2.10F &&
            GizmoStyle::MoveAxisDraggingThicknessPixels >= 1.85F &&
            GizmoStyle::MoveAxisDraggingThicknessPixels <= 2.05F,
        "Move and Rotate must share one soft palette and a compact visual hierarchy.");

    constexpr std::array<TransformGizmoAxis, 3U> axes{
        TransformGizmoAxis::X,
        TransformGizmoAxis::Y,
        TransformGizmoAxis::Z};
    constexpr std::array<std::string_view, 3U> hoverHelp{
        "Move X", "Move Y", "Move Z"};
    constexpr std::array<std::string_view, 3U> dragHelp{
        "Moving on X — Release to apply — Esc to cancel",
        "Moving on Y — Release to apply — Esc to cancel",
        "Moving on Z — Release to apply — Esc to cancel"};
    for (std::size_t activeIndex = 0U; activeIndex < axes.size(); ++activeIndex)
    {
        context.InteractionState = TransformGizmoInteractionState::Hover;
        context.ActiveAxis = axes[activeIndex];
        Require(model.Update(context), "Unable to build a hovered Move style.");
        const TransformGizmoView hovered = model.View();
        const float thicknessRatio =
            hovered.Axes[activeIndex].Thickness /
            normal.Axes[activeIndex].Thickness;
        Require((hovered.Axes[activeIndex].CameraFacing ||
                    (thicknessRatio >= 1.10F && thicknessRatio <= 1.20F)) &&
                hovered.Axes[activeIndex].Color !=
                    normal.Axes[activeIndex].Color &&
                TransformGizmoModel::ContextHelpFor(
                    hovered.State, hovered.ActiveAxis) == hoverHelp[activeIndex],
            "Hover must brighten its axis and restrain visible shaft thickening.");
        for (std::size_t index = 0U; index < axes.size(); ++index)
        {
            if (index == activeIndex) continue;
            Require(hovered.Axes[index].Color == normal.Axes[index].Color &&
                    hovered.Axes[index].Thickness ==
                        normal.Axes[index].Thickness,
                "Hover must leave both inactive axes visually unchanged.");
        }

        context.InteractionState = TransformGizmoInteractionState::Dragging;
        Require(model.Update(context), "Unable to build a dragged Move style.");
        const TransformGizmoView dragged = model.View();
        Require(dragged.Axes[activeIndex].Color ==
                    colors[activeIndex] &&
                (dragged.Axes[activeIndex].CameraFacing ||
                    (dragged.Axes[activeIndex].Thickness <
                        hovered.Axes[activeIndex].Thickness &&
                     dragged.Axes[activeIndex].Thickness >
                        normal.Axes[activeIndex].Thickness)) &&
                TransformGizmoModel::ContextHelpFor(
                    dragged.State, dragged.ActiveAxis) == dragHelp[activeIndex],
            "Dragging must lock the highlighted shaft, tip, and contextual help.");
        for (std::size_t index = 0U; index < axes.size(); ++index)
        {
            if (index == activeIndex) continue;
            Require(dragged.Axes[index].Color[3] == 1.0F &&
                    dragged.Axes[index].Color[0] < normal.Axes[index].Color[0] &&
                    dragged.Axes[index].Color[1] < normal.Axes[index].Color[1] &&
                    dragged.Axes[index].Color[2] < normal.Axes[index].Color[2] &&
                    dragged.Axes[index].Color[0] >
                        normal.Axes[index].Color[0] * 0.65F,
                "Inactive drag axes must remain visible with restrained dimming.");
        }
    }

    context.InteractionState = TransformGizmoInteractionState::Idle;
    context.ActiveAxis = TransformGizmoAxis::None;
    Require(model.Update(context) && model.View().Axes == normal.Axes &&
            TransformGizmoModel::ContextHelpFor(
                context.InteractionState, context.ActiveAxis).empty(),
        "MouseUp, Esc, or tool reset must restore the exact normal appearance.");
    Require(TransformGizmoModel::ContextHelpFor(
                TransformGizmoInteractionState::Hover,
                TransformGizmoAxis::None).empty(),
        "No axis must produce no contextual gizmo help.");
    for (const ActiveVoxelTool tool : {
             ActiveVoxelTool::Rotate, ActiveVoxelTool::Scale})
    {
        context.ActiveTool = tool;
        Require(model.Update(context) &&
                std::ranges::none_of(model.View().Axes,
                    [](const TransformGizmoAxisView& axis)
                    {
                        return axis.HasArrowHead;
                    }),
            "Rotate and Scale must remain free of Move arrow heads.");
    }
}
}

int main()
{
    try
    {
        TestPickingAndPriority();
        TestPickingMatchesHybridRenderedSegmentsAtEveryDepth();
        TestArrowHeadsAndPickingUseTheFinalProjection();
        TestShaftAndArrowTipPickingStaySynchronized();
        TestAxisLockedIntegerDragging();
        TestRoundingRayGeometryAndParallelFallback();
        TestInvalidationCancellationAndCaptureLifetime();
        TestVisualStateIsPreparedOutsideRenderer();
        TestProfessionalMoveGizmoStyleAndContextHelp();
    }
    catch (const std::exception& error)
    {
        std::cerr << "MoveGizmoTests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "MoveGizmoTests passed\n";
    return EXIT_SUCCESS;
}
