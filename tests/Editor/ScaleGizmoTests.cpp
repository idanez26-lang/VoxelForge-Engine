#include "Transform/ScaleVoxelSelectionOperation.h"
#include "TransformGizmo/GizmoStyle.h"
#include "TransformGizmo/TransformGizmoInteraction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

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

[[nodiscard]] TransformGizmoUpdateContext Context(
    const ActiveVoxelTool tool = ActiveVoxelTool::Scale)
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 9U;
    context.SelectionDocumentGeneration = 9U;
    context.Bounds = SelectionBounds::FromCorners({2, 3, 4}, {7, 8, 9});
    context.ModelCenter = {5.0F, 6.0F, 7.0F};
    context.ActiveTool = tool;
    context.CameraPosition = {0.0F, 0.0F, -20.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 1000.0F;
    context.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
    context.ViewProjection = Projection();
    return context;
}

[[nodiscard]] Vec2 Project(
    const Vec3 point, const TransformGizmoPointerInput& input)
{
    const auto projected = TransformGizmoModel::ProjectWorldToScreen(
        point, input.Viewport, input.ViewProjection);
    Require(projected.has_value(), "Scale gizmo point did not project.");
    return *projected;
}

void TestVisibilityStyleAndHandles()
{
    TransformGizmoModel model;
    auto context = Context();
    context.SelectionEmpty = true;
    Require(!model.Update(context) && !model.View().Visible,
        "Scale gizmo must stay hidden without a selection.");
    context = Context(ActiveVoxelTool::Move);
    Require(model.Update(context) &&
            model.View().Mode == TransformGizmoMode::Move &&
            std::ranges::none_of(model.View().Axes,
                [](const TransformGizmoAxisView& axis)
                { return axis.HasScaleHandle; }),
        "Move must not receive Scale handles.");

    context = Context();
    Require(model.Update(context), "Unable to build Scale gizmo.");
    const TransformGizmoView idle = model.View();
    Require(idle.Visible && idle.Mode == TransformGizmoMode::Scale &&
            idle.State == TransformGizmoInteractionState::Idle,
        "Scale must expose one visible idle gizmo.");
    const std::array baseColors{
        GizmoStyle::AxisColorX, GizmoStyle::AxisColorY,
        GizmoStyle::AxisColorZ};
    for (std::size_t index = 0U; index < idle.Axes.size(); ++index)
    {
        const auto& axis = idle.Axes[index];
        Require(axis.HasScaleHandle && !axis.HasArrowHead &&
                !axis.HasRotationRing &&
                axis.ScaleHandleSizePixels ==
                    GizmoStyle::ScaleHandleIdleSizePixels &&
                axis.Thickness > 0.0F &&
                std::abs(axis.Color[0] - baseColors[index][0] *
                    GizmoStyle::IdleIntensity) < 0.001F &&
                std::abs(axis.Color[1] - baseColors[index][1] *
                    GizmoStyle::IdleIntensity) < 0.001F &&
                std::abs(axis.Color[2] - baseColors[index][2] *
                    GizmoStyle::IdleIntensity) < 0.001F,
            "Scale axes must use shared colors, thickness, and flat handles.");
    }
    Require(GizmoStyle::ScaleHandleIdleSizePixels >= 6.0F &&
            GizmoStyle::ScaleHandleIdleSizePixels <= 8.0F,
        "Idle Scale handles must stay compact.");
}

void TestPickingHoverAndIntegerDragging()
{
    TransformGizmoModel model;
    auto context = Context();
    Require(model.Update(context), "Unable to create Scale picking fixture.");
    TransformGizmoView view = model.View();
    TransformGizmoPointerInput input;
    input.Viewport = context.Viewport;
    input.ViewProjection = context.ViewProjection;
    const SelectionBounds bounds = context.Bounds;

    for (std::size_t index = 0U; index < view.Axes.size(); ++index)
    {
        TransformGizmoInteraction interaction;
        const TransformGizmoAxisView& axisView = view.Axes[index];
        input.ScreenPosition = Project(axisView.End, input);
        Require(interaction.UpdateHover(view, input) == axisView.Axis,
            "Each Scale terminal handle must be pickable.");
        context.InteractionState = TransformGizmoInteractionState::Hover;
        context.ActiveAxis = axisView.Axis;
        Require(model.Update(context), "Unable to style Scale hover.");
        const TransformGizmoView hovered = model.View();
        Require(hovered.Axes[index].ScaleHandleSizePixels ==
                    GizmoStyle::ScaleHandleHoverSizePixels &&
                hovered.Axes[index].Color != axisView.Color,
            "Scale hover must synchronize segment and handle styling.");

        Require(interaction.BeginDrag(
                view, axisView.Axis, input, 9U, bounds) &&
                interaction.Mode() == TransformGizmoMode::Scale,
            "Each Scale axis must begin a locked drag.");
        const Vec2 start = Project(axisView.Start, input);
        const Vec2 end = Project(axisView.End, input);
        const float dx = end.X - start.X;
        const float dy = end.Y - start.Y;
        const float pixels = std::sqrt(dx * dx + dy * dy);
        Require(pixels > 1.0F, "Scale test axis projection is degenerate.");
        const float worldLength = Length(axisView.End - axisView.Start);
        const float pixelsForTwoVoxels = 2.0F * pixels / worldLength;
        input.ScreenPosition = {
            input.ScreenPosition.X + dx / pixels * pixelsForTwoVoxels,
            input.ScreenPosition.Y + dy / pixels * pixelsForTwoVoxels};
        Require(interaction.UpdateDrag(input) &&
                interaction.LockedAxis() == axisView.Axis,
            "Positive Scale drag must remain locked to its original axis.");
        auto target = interaction.TargetDimensions();
        Require((axisView.Axis != TransformGizmoAxis::X || target.X == 8U) &&
                (axisView.Axis != TransformGizmoAxis::Y || target.Y == 8U) &&
                (axisView.Axis != TransformGizmoAxis::Z || target.Z == 8U),
            "Positive Scale drag must produce an integer target dimension.");
        const TransformGizmoDragRelease release = interaction.EndDrag();
        Require(release.WasDragging &&
                release.Mode == TransformGizmoMode::Scale &&
                release.Axis == axisView.Axis &&
                release.TargetDimensions == target,
            "MouseUp must preserve the Scale target dimension.");
    }

    TransformGizmoInteraction minimum;
    view = TransformGizmoModel{}.View();
    TransformGizmoModel minimumModel;
    Require(minimumModel.Update(Context()), "Unable to rebuild Scale fixture.");
    view = minimumModel.View();
    input.ScreenPosition = Project(view.Axes[0].End, input);
    Require(minimum.UpdateHover(view, input) == TransformGizmoAxis::X &&
            minimum.BeginDrag(view, TransformGizmoAxis::X, input, 9U, bounds),
        "Unable to start minimum-dimension Scale drag.");
    input.ScreenPosition.X -= 100000.0F;
    static_cast<void>(minimum.UpdateDrag(input));
    Require(minimum.TargetDimensions().X == 1U &&
            minimum.TargetDimensions().Y == 6U &&
            minimum.TargetDimensions().Z == 6U,
        "Negative Scale drag must clamp the active dimension to one.");
    Require(!minimum.UpdateDrag(input),
        "An unchanged minimum target must not rebuild the Scale preview.");
    Require(minimum.Cancel() && !minimum.IsDragging(),
        "Esc must cancel Scale drag without residue.");
}

void TestContextHelpAndInvalidation()
{
    const std::array axes{
        TransformGizmoAxis::X, TransformGizmoAxis::Y,
        TransformGizmoAxis::Z};
    for (const TransformGizmoAxis axis : axes)
    {
        Require(!TransformGizmoModel::ContextHelpFor(
                    TransformGizmoMode::Scale,
                    TransformGizmoInteractionState::Hover, axis).empty() &&
                !TransformGizmoModel::ContextHelpFor(
                    TransformGizmoMode::Scale,
                    TransformGizmoInteractionState::Dragging, axis).empty(),
            "Scale hover and drag need contextual help on every axis.");
    }

    TransformGizmoModel model;
    const auto context = Context();
    Require(model.Update(context), "Unable to create invalidation fixture.");
    TransformGizmoPointerInput input;
    input.Viewport = context.Viewport;
    input.ViewProjection = context.ViewProjection;
    input.ScreenPosition = Project(model.View().Axes[2].End, input);
    TransformGizmoInteraction interaction;
    Require(interaction.UpdateHover(model.View(), input) ==
                TransformGizmoAxis::Z &&
            interaction.BeginDrag(model.View(), TransformGizmoAxis::Z,
                input, 9U, context.Bounds) &&
            !interaction.Validate(10U, context.Bounds, true) &&
            !interaction.IsDragging(),
        "Changing the document must cancel an active Scale drag.");
}
}

int main()
{
    try
    {
        TestVisibilityStyleAndHandles();
        TestPickingHoverAndIntegerDragging();
        TestContextHelpAndInvalidation();
        std::cout << "Scale Gizmo tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Scale Gizmo tests failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
