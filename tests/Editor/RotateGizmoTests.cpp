#include "Transform/RotateVoxelSelectionOperation.h"
#include "TransformGizmo/GizmoStyle.h"
#include "TransformGizmo/TransformGizmoInteraction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;
using VoxelForge::Asset::Voxel::VoxelPosition;

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
    const float depth = 20.0F)
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 9U;
    context.SelectionDocumentGeneration = 9U;
    context.Bounds = SelectionBounds::FromCorners({2, 3, 4}, {7, 8, 9});
    context.PivotValid = true;
    context.PivotWorldPosition = {};
    context.ActiveTool = ActiveVoxelTool::Rotate;
    context.CameraPosition = {0.0F, 0.0F, -depth};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 1000.0F;
    context.Viewport = {0.0F, 0.0F, 1000.0F, 1000.0F};
    context.ViewProjection = Projection();
    return context;
}

[[nodiscard]] TransformGizmoView RotateView()
{
    TransformGizmoModel model;
    Require(model.Update(Context()) && model.View().Visible,
        "Unable to build the Rotate gizmo fixture.");
    return model.View();
}

[[nodiscard]] Vec2 Project(
    const Vec3 point, const TransformGizmoPointerInput& input)
{
    const auto projected = TransformGizmoModel::ProjectWorldToScreen(
        point, input.Viewport, input.ViewProjection);
    Require(projected.has_value(), "A ring point did not project.");
    return *projected;
}

[[nodiscard]] float DistanceSquared(const Vec2 a, const Vec2 b)
{
    const float x = a.X - b.X;
    const float y = a.Y - b.Y;
    return x * x + y * y;
}

[[nodiscard]] Vec2 DistinctRingPoint(
    const TransformGizmoView& view,
    const std::size_t target,
    const TransformGizmoPointerInput& input)
{
    float bestSeparation = -1.0F;
    Vec2 best{};
    for (const Vec3 point : view.Axes[target].RotationRingPoints)
    {
        const Vec2 screen = Project(point, input);
        float separation = 1.0e9F;
        for (std::size_t other = 0U; other < view.Axes.size(); ++other)
        {
            if (other == target) continue;
            for (const Vec3 candidate : view.Axes[other].RotationRingPoints)
                separation = std::min(
                    separation, DistanceSquared(screen, Project(candidate, input)));
        }
        if (separation > bestSeparation)
        {
            bestSeparation = separation;
            best = screen;
        }
    }
    Require(bestSeparation > 1.0F,
        "Projected rings need at least one deterministic pick point.");
    return best;
}

void TestRingGeometryAndHybridSizing()
{
    const TransformGizmoView view = RotateView();
    Require(view.Mode == TransformGizmoMode::Rotate && view.Visible &&
            view.State == TransformGizmoInteractionState::Idle,
        "Rotate must expose one visible idle gizmo.");
    for (const TransformGizmoAxisView& axis : view.Axes)
    {
        Require(axis.HasRotationRing && !axis.HasArrowHead &&
                axis.ArrowLength == 0.0F && axis.ArrowWidth == 0.0F &&
                axis.RotationRingRadius > 0.0F &&
                axis.RotationRingPoints.size() == 128U,
            "Every Rotate axis needs one smooth 128-segment ring and no arrow.");
        for (const Vec3 point : axis.RotationRingPoints)
        {
            const Vec3 relative = point - view.Center;
            Require(std::abs(Length(relative) - axis.RotationRingRadius) < 0.001F,
                "Ring points must remain on the same radius.");
            Require((axis.Axis != TransformGizmoAxis::X ||
                        std::abs(relative.X) < 0.0001F) &&
                    (axis.Axis != TransformGizmoAxis::Y ||
                        std::abs(relative.Y) < 0.0001F) &&
                    (axis.Axis != TransformGizmoAxis::Z ||
                        std::abs(relative.Z) < 0.0001F),
                "X/Y/Z rings must lie in YZ/XZ/XY respectively.");
        }
    }
    Require(GizmoStyle::RotateXColor[0] > GizmoStyle::RotateXColor[1] &&
            GizmoStyle::RotateXColor[0] > GizmoStyle::RotateXColor[2] &&
            GizmoStyle::RotateYColor[1] > GizmoStyle::RotateYColor[0] &&
            GizmoStyle::RotateYColor[1] > GizmoStyle::RotateYColor[2] &&
            GizmoStyle::RotateZColor[2] > GizmoStyle::RotateZColor[0] &&
            GizmoStyle::RotateZColor[2] > GizmoStyle::RotateZColor[1] &&
            GizmoStyle::RotateXColor != GizmoStyle::RotateYColor &&
            GizmoStyle::RotateYColor != GizmoStyle::RotateZColor &&
            GizmoStyle::RotateXColor != GizmoStyle::RotateZColor,
        "X/Y/Z Rotate colors must be distinct, non-pure axis colors.");
    const auto visualPixels = [](const TransformGizmoAxisView& ring)
    {
        return ring.Thickness * ring.ProjectedLengthPixels /
            ring.RotationRingRadius;
    };
    for (std::size_t index = 0U; index < view.Axes.size(); ++index)
    {
        const TransformGizmoAxisView& ring = view.Axes[index];
        Require(visualPixels(ring) >= 1.55F && visualPixels(ring) <= 1.75F,
            "Idle Rotate rings must stay inside the final visual target.");
        const auto base = index == 0U ? GizmoStyle::RotateXColor :
            index == 1U ? GizmoStyle::RotateYColor :
            GizmoStyle::RotateZColor;
        Require(std::abs(ring.Color[0] -
                    base[0] * GizmoStyle::RotateIdleIntensity) < 0.001F &&
                std::abs(ring.Color[1] -
                    base[1] * GizmoStyle::RotateIdleIntensity) < 0.001F &&
                std::abs(ring.Color[2] -
                    base[2] * GizmoStyle::RotateIdleIntensity) < 0.001F &&
                base[0] < 0.95F && base[1] < 0.95F && base[2] < 0.95F,
            "Idle colors must use the centralized soft axis palette.");
    }
    constexpr float previousRadiusTarget = 6.0F *
        TransformGizmoModel::SelectionRelativeFactor;
    Require(GizmoStyle::RotateRadiusMultiplier == 1.30F &&
            view.Axes[0].RotationRingRadius > previousRadiusTarget &&
            view.Axes[1].RotationRingRadius > previousRadiusTarget &&
            view.Axes[2].RotationRingRadius > previousRadiusTarget,
        "Rotate radius must exceed the old 0.35-extent target before the existing screen cap.");
    const float representativeLength =
        (view.Axes[0].RotationRingRadius + view.Axes[1].RotationRingRadius +
         view.Axes[2].RotationRingRadius) / 3.0F;
    const float representativePixels = std::max(1.0F,
        (view.Axes[0].ProjectedLengthPixels +
         view.Axes[1].ProjectedLengthPixels +
         view.Axes[2].ProjectedLengthPixels) / 3.0F);
    const float centerDiameterPixels = 2.0F * view.CenterRadius *
        representativePixels / representativeLength;
    Require(std::abs(centerDiameterPixels -
                GizmoStyle::RotateCenterDiameterPixels) < 0.05F,
        "Rotate center must prepare the compact screen-space diameter.");
    Require(GizmoStyle::RotateIdleIntensity >= 0.68F &&
            GizmoStyle::RotateIdleIntensity <= 0.74F &&
            GizmoStyle::RotateHoverIntensity >= 0.95F &&
            GizmoStyle::RotateHoverIntensity <= 1.0F &&
            GizmoStyle::RotateDraggingIntensity == 1.0F &&
            GizmoStyle::RotateInactiveDraggingIntensity >= 0.45F &&
            GizmoStyle::RotateInactiveDraggingIntensity <= 0.55F &&
            GizmoStyle::PickingTolerancePixels == 8.0F &&
            GizmoStyle::PickingTolerancePixels >=
                GizmoStyle::RotateHoverThicknessPixels * 4.0F,
        "Final Rotate styling and independent picking must stay in range.");
    Require(GizmoStyle::RotateMaximumChordPixels > 0.0F &&
            GizmoStyle::RotateNearPlaneDistance > 0.0F &&
            GizmoStyle::RotateOccludedIntensity >= 0.45F &&
            GizmoStyle::RotateOccludedIntensity <= 0.55F,
        "Rotate overlay must reject invalid projected extensions.");

    TransformGizmoModel nearModel;
    TransformGizmoModel farModel;
    auto nearContext = Context(0.08F);
    auto farContext = Context(500.0F);
    Require(nearModel.Update(nearContext) && farModel.Update(farContext) &&
            nearModel.View().AxisLength <=
                TransformGizmoModel::MaximumWorldLength &&
            farModel.View().AxisLength > 0.0F,
        "Rotate rings must remain finite near and visible far away.");

    auto hover = Context();
    hover.InteractionState = TransformGizmoInteractionState::Hover;
    hover.ActiveAxis = TransformGizmoAxis::Y;
    TransformGizmoModel styled;
    Require(styled.Update(hover), "Unable to style hovered Rotate ring.");
    const auto hovered = styled.View();
    hover.InteractionState = TransformGizmoInteractionState::Dragging;
    Require(styled.Update(hover), "Unable to style dragged Rotate ring.");
    const auto dragged = styled.View();
    Require(visualPixels(hovered.Axes[1]) >= 1.80F &&
            visualPixels(hovered.Axes[1]) <= 2.00F &&
            visualPixels(hovered.Axes[1]) /
                visualPixels(view.Axes[1]) >= 1.12F &&
            visualPixels(hovered.Axes[1]) /
                visualPixels(view.Axes[1]) <= 1.18F &&
            visualPixels(dragged.Axes[1]) >= 1.75F &&
            visualPixels(dragged.Axes[1]) <= 1.95F &&
            hovered.Axes[1].Thickness > view.Axes[1].Thickness &&
            hovered.Axes[0].Color == view.Axes[0].Color &&
            hovered.Axes[2].Color == view.Axes[2].Color &&
            dragged.Axes[1].Color[1] > hovered.Axes[1].Color[1] &&
            dragged.Axes[0].Color[0] < hovered.Axes[0].Color[0] &&
            dragged.Axes[2].Color[2] < hovered.Axes[2].Color[2] &&
            !TransformGizmoModel::ContextHelpFor(
                TransformGizmoMode::Rotate, dragged.State,
                dragged.ActiveAxis).empty(),
        "Hover and dragging must highlight the ring and dim inactive axes.");
}

void TestPickingDraggingAndCancellation()
{
    const TransformGizmoView view = RotateView();
    TransformGizmoPointerInput input;
    input.Viewport = Context().Viewport;
    input.ViewProjection = Projection();
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({2, 3, 4}, {7, 8, 9});

    for (std::size_t index = 0U; index < view.Axes.size(); ++index)
    {
        TransformGizmoInteraction interaction;
        input.ScreenPosition = DistinctRingPoint(view, index, input);
        const TransformGizmoAxis axis = view.Axes[index].Axis;
        Require(interaction.UpdateHover(view, input) == axis &&
                interaction.BeginDrag(view, axis, input, 9U, bounds),
            "Every visible Rotate ring must be pickable and draggable.");
        const Vec2 center = Project(view.Center, input);
        const Vec2 start{input.ScreenPosition.X - center.X,
                         input.ScreenPosition.Y - center.Y};
        input.ScreenPosition = {center.X - start.Y, center.Y + start.X};
        Require(interaction.UpdateDrag(input) &&
                std::abs(interaction.QuarterTurns()) == 1 &&
                interaction.Mode() == TransformGizmoMode::Rotate,
            "A quarter-circle drag must quantize to exactly one turn.");
        Require(!interaction.UpdateDrag(input),
            "An unchanged pointer must not rebuild the Rotate preview.");
        const TransformGizmoDragRelease release = interaction.EndDrag();
        Require(release.WasDragging && release.Mode == TransformGizmoMode::Rotate &&
                release.Axis == axis && std::abs(release.QuarterTurns) == 1,
            "MouseUp must release one quantized Rotate operation.");
    }

    TransformGizmoInteraction cancelled;
    input.ScreenPosition = DistinctRingPoint(view, 2U, input);
    Require(cancelled.UpdateHover(view, input) == TransformGizmoAxis::Z &&
            cancelled.BeginDrag(view, TransformGizmoAxis::Z, input, 9U, bounds) &&
            cancelled.Cancel() && !cancelled.IsDragging(),
        "Esc/tool cancellation must clear a Rotate drag without residue.");

    TransformGizmoView hidden = view;
    hidden.Visible = false;
    Require(cancelled.UpdateHover(hidden, input) == TransformGizmoAxis::None,
        "A hidden Rotate gizmo must not capture input.");
}

void TestOrthogonalVoxelGeometry()
{
    const std::vector<VoxelPosition> source{
        {2, 3, 4}, {2, 4, 4}, {2, 5, 4}};
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({2, 3, 4}, {2, 5, 4});
    const auto aroundX = RotateVoxelSelectionOperation::BuildGeometry(
        source, bounds, VoxelRotationAxis::X, 1);
    Require(aroundX.Valid() && aroundX.Bounds.Dimensions() ==
            VoxelForge::Asset::Voxel::VoxelDimensions{1U, 1U, 3U},
        "Rotate X must exchange Y/Z extents.");

    const std::vector<VoxelPosition> xLine{
        {2, 3, 4}, {3, 3, 4}, {4, 3, 4}};
    const SelectionBounds xBounds =
        SelectionBounds::FromCorners({2, 3, 4}, {4, 3, 4});
    const auto aroundY = RotateVoxelSelectionOperation::BuildGeometry(
        xLine, xBounds, VoxelRotationAxis::Y, 1);
    const auto aroundZ = RotateVoxelSelectionOperation::BuildGeometry(
        xLine, xBounds, VoxelRotationAxis::Z, 1);
    Require(aroundY.Valid() && aroundY.Bounds.Dimensions() ==
            VoxelForge::Asset::Voxel::VoxelDimensions{1U, 1U, 3U} &&
            aroundZ.Valid() && aroundZ.Bounds.Dimensions() ==
            VoxelForge::Asset::Voxel::VoxelDimensions{1U, 3U, 1U},
        "Rotate Y/Z must exchange the correct orthogonal extents.");
    const auto halfTurn = RotateVoxelSelectionOperation::BuildGeometry(
        xLine, xBounds, VoxelRotationAxis::Z, 2);
    const auto fullTurn = RotateVoxelSelectionOperation::BuildGeometry(
        xLine, xBounds, VoxelRotationAxis::Z, 4);
    auto sorted = [](std::vector<VoxelPosition> values)
    {
        std::ranges::sort(values, {}, [](const VoxelPosition value)
        { return std::array{value.X, value.Y, value.Z}; });
        return values;
    };
    Require(halfTurn.Valid() && fullTurn.Valid() &&
            sorted(fullTurn.Destinations) == sorted(xLine) &&
            halfTurn.Destinations.size() == xLine.size(),
        "Half and full turns must remain deterministic and bijective.");
}
}

int main()
{
    try
    {
        TestRingGeometryAndHybridSizing();
        TestPickingDraggingAndCancellation();
        TestOrthogonalVoxelGeometry();
        std::cout << "Rotate Gizmo tests passed.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Rotate Gizmo tests failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
