#include "TransformGizmo/TransformGizmoModel.h"
#include "EditorMatrix.h"
#include "Transform/TransformPivotManager.h"
#include "VoxelHistory/VoxelEditHistory.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cmath>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using VoxelForge::Asset::Voxel::VoxelPosition;
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void ResolvePivot(
    TransformGizmoUpdateContext& context,
    const Vec3 modelCenter = {})
{
    TransformPivotManager manager;
    static_cast<void>(manager.UpdateFromBounds(context.Bounds, modelCenter));
    context.PivotValid = manager.HasValidPivot();
    context.PivotWorldPosition = context.PivotValid
        ? manager.GetPivot().WorldPosition : Vec3{};
}

[[nodiscard]] bool Near(
    const float left, const float right, const float tolerance = 0.0001F)
{
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] TransformGizmoUpdateContext MakeContext(
    const SelectionBounds bounds =
        SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}),
    const ActiveVoxelTool tool = ActiveVoxelTool::Move)
{
    TransformGizmoUpdateContext context;
    context.DocumentActive = true;
    context.SelectionEmpty = false;
    context.ActiveDocumentGeneration = 7U;
    context.SelectionDocumentGeneration = 7U;
    context.Bounds = bounds;
    context.ActiveTool = tool;
    ResolvePivot(context);
    context.CameraPosition = {0.0F, 0.0F, -10.0F};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.VerticalFieldOfViewDegrees = 45.0F;
    context.ViewportHeightPixels = 900.0F;
    return context;
}

[[nodiscard]] float ReprojectXAxisPixels(
    const float worldLength,
    const float depth,
    const float verticalFovDegrees,
    const float viewportWidth,
    const float viewportHeight)
{
    const float yScale = 1.0F / std::tan(
        DegreesToRadians(verticalFovDegrees) * 0.5F);
    const float xScale = yScale / (viewportWidth / viewportHeight);
    const Matrix4 projection{
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F};
    const Vec3 start = TransformPoint(projection, {0.0F, 0.0F, depth});
    const Vec3 end = TransformPoint(
        projection, {worldLength, 0.0F, depth});
    const float startPixel = (start.X * 0.5F + 0.5F) * viewportWidth;
    const float endPixel = (end.X * 0.5F + 0.5F) * viewportWidth;
    return std::abs(endPixel - startPixel);
}

[[nodiscard]] Matrix4 MakeViewProjection(
    const Vec3 eye,
    const Vec3 forwardValue,
    const float verticalFovDegrees,
    const float viewportWidth,
    const float viewportHeight)
{
    const Vec3 forward = Normalize(forwardValue);
    const Vec3 referenceUp = std::abs(Dot(forward, {0.0F, 1.0F, 0.0F})) >
            0.98F
        ? Vec3{0.0F, 0.0F, 1.0F}
        : Vec3{0.0F, 1.0F, 0.0F};
    const Vec3 right = Normalize(Cross(forward, referenceUp));
    const Vec3 up = Normalize(Cross(right, forward));
    const Matrix4 view{
        right.X, right.Y, right.Z, -Dot(right, eye),
        up.X, up.Y, up.Z, -Dot(up, eye),
        forward.X, forward.Y, forward.Z, -Dot(forward, eye),
        0.0F, 0.0F, 0.0F, 1.0F};
    const float yScale = 1.0F / std::tan(
        DegreesToRadians(verticalFovDegrees) * 0.5F);
    const float xScale = yScale / (viewportWidth / viewportHeight);
    constexpr float nearPlane = 0.05F;
    constexpr float farPlane = 100000.0F;
    const float depthScale = farPlane / (farPlane - nearPlane);
    const Matrix4 projection{
        xScale, 0.0F, 0.0F, 0.0F,
        0.0F, yScale, 0.0F, 0.0F,
        0.0F, 0.0F, depthScale, -nearPlane * depthScale,
        0.0F, 0.0F, 1.0F, 0.0F};
    return MultiplyMatrix(projection, view);
}

[[nodiscard]] TransformGizmoUpdateContext MakeProjectedContext(
    const Vec3 center,
    const Vec3 forward,
    const float depth,
    const float viewportWidth = 1280.0F,
    const float viewportHeight = 720.0F,
    const float fieldOfView = 45.0F)
{
    TransformGizmoUpdateContext context = MakeContext();
    context.PivotValid = true;
    context.PivotWorldPosition = center;
    context.CameraForward = Normalize(forward);
    context.CameraPosition = center - context.CameraForward * depth;
    context.VerticalFieldOfViewDegrees = fieldOfView;
    context.ViewportHeightPixels = viewportHeight;
    context.Viewport = {0.0F, 0.0F, viewportWidth, viewportHeight};
    context.ViewProjection = MakeViewProjection(
        context.CameraPosition, context.CameraForward, fieldOfView,
        viewportWidth, viewportHeight);
    return context;
}

[[nodiscard]] float ProjectedLength(
    const TransformGizmoAxisView& axis,
    const TransformGizmoUpdateContext& context)
{
    const auto start = TransformGizmoModel::ProjectWorldToScreen(
        axis.Start, context.Viewport, context.ViewProjection);
    const auto end = TransformGizmoModel::ProjectWorldToScreen(
        axis.End, context.Viewport, context.ViewProjection);
    if (!start || !end) return 0.0F;
    const float dx = end->X - start->X;
    const float dy = end->Y - start->Y;
    return std::sqrt(dx * dx + dy * dy);
}

void TestVisibilityModesAndStateMachine()
{
    TransformGizmoModel model;
    Require(!model.View().Visible &&
            model.View().State == TransformGizmoInteractionState::Hidden &&
            model.View().Mode == TransformGizmoMode::None &&
            model.View().ActiveAxis == TransformGizmoAxis::None,
        "The initial gizmo state must be fully hidden and neutral.");

    auto context = MakeContext();
    context.DocumentActive = false;
    Require(!model.Update(context) && !model.View().Visible,
        "No document must keep the initial gizmo hidden.");
    context = MakeContext();
    context.SelectionEmpty = true;
    Require(!model.Update(context) && !model.View().Visible,
        "An empty selection must keep the gizmo hidden.");
    context = MakeContext({}, ActiveVoxelTool::Pencil);
    Require(!model.Update(context) && !model.View().Visible,
        "An incompatible tool must keep the gizmo hidden.");
    context = MakeContext();
    context.SelectionDocumentGeneration = 6U;
    Require(!model.Update(context) && !model.View().Visible,
        "A stale selection generation must keep the gizmo hidden.");
    context = MakeContext();
    context.PivotValid = false;
    Require(!model.Update(context) && !model.View().Visible,
        "A missing resolved pivot must keep the gizmo hidden.");

    for (const auto [tool, mode] : {
             std::pair{ActiveVoxelTool::Move, TransformGizmoMode::Move},
             std::pair{ActiveVoxelTool::Rotate, TransformGizmoMode::Rotate},
             std::pair{ActiveVoxelTool::Scale, TransformGizmoMode::Scale}})
    {
        context = MakeContext(
            SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1}), tool);
        Require(model.Update(context) && model.View().Visible &&
                model.View().Mode == mode &&
                model.View().State == TransformGizmoInteractionState::Idle &&
                model.View().ActiveAxis == TransformGizmoAxis::None,
            "Move, Rotate and Scale must synchronize to an idle gizmo.");
    }
    context.Closing = true;
    Require(model.Update(context) && !model.View().Visible,
        "Closing must purge the gizmo.");
    model.Reset();
    Require(!model.View().Visible &&
            model.View().ActiveAxis == TransformGizmoAxis::None &&
            model.View().Mode == TransformGizmoMode::None,
        "Reset must purge mode, axis and visibility.");
}

void TestExactSpatialCentersAndInvalidation()
{
    TransformGizmoModel model;
    auto context = MakeContext(
        SelectionBounds::FromCorners({4, 2, 7}, {4, 2, 7}));
    Require(model.Update(context) &&
            model.View().Center == Vec3{4.5F, 2.5F, 7.5F},
        "A single voxel must be centered on the cell center.");

    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});
    ResolvePivot(context);
    Require(model.Update(context) && model.View().Center == Vec3{1, 1, 1},
        "Even selection dimensions must use spatial bounds.");
    context.Bounds = SelectionBounds::FromCorners({0, 0, 0}, {2, 2, 2});
    ResolvePivot(context);
    Require(model.Update(context) &&
            model.View().Center == Vec3{1.5F, 1.5F, 1.5F},
        "Odd selection dimensions must use spatial bounds.");
    context.Bounds = SelectionBounds::FromCorners({2, 4, 6}, {7, 8, 10});
    ResolvePivot(context, {3.0F, 2.0F, 1.0F});
    Require(model.Update(context) &&
            model.View().Center == Vec3{2.0F, 4.5F, 7.5F},
        "Asymmetric bounds must be translated by the viewport model center.");

    const Vec3 movedCenter = model.View().Center;
    context.Bounds = SelectionBounds::FromCorners({5, 4, 6}, {10, 8, 10});
    ResolvePivot(context, {3.0F, 2.0F, 1.0F});
    Require(model.Update(context) &&
            model.View().Center == movedCenter + Vec3{3.0F, 0.0F, 0.0F},
        "Move must update the center from the current bounds.");
    context.Bounds = SelectionBounds::FromCorners({2, 4, 6}, {7, 8, 10});
    ResolvePivot(context, {3.0F, 2.0F, 1.0F});
    Require(model.Update(context) && model.View().Center == movedCenter,
        "Undo must restore the center from restored bounds.");
    context.Bounds = SelectionBounds::FromCorners({5, 4, 6}, {10, 8, 10});
    ResolvePivot(context, {3.0F, 2.0F, 1.0F});
    Require(model.Update(context) &&
            model.View().Center == movedCenter + Vec3{3.0F, 0.0F, 0.0F},
        "Redo must restore the moved center.");

    context.SelectionEmpty = true;
    Require(model.Update(context) && !model.View().Visible,
        "Clearing selection must invalidate the previous center.");
    context = MakeContext();
    Require(model.Update(context) && model.View().Visible,
        "A valid replacement document selection must rebuild the gizmo.");
    context.ActiveDocumentGeneration = 8U;
    Require(model.Update(context) && !model.View().Visible,
        "Changing document generation must purge the old gizmo.");
}

void TestHybridScaleAndDegenerateInputs()
{
    auto context = MakeContext();
    context.CameraPosition = {};
    context.CameraForward = {0.0F, 0.0F, 1.0F};
    context.ViewportHeightPixels = 720.0F;
    constexpr float viewportWidth = 1280.0F;
    context.Viewport = {0.0F, 0.0F, viewportWidth,
        context.ViewportHeightPixels};
    context.ViewProjection = MakeViewProjection(
        context.CameraPosition, context.CameraForward,
        context.VerticalFieldOfViewDegrees,
        viewportWidth, context.ViewportHeightPixels);
    const auto sizingAt = [&](const float depth)
    {
        return TransformGizmoModel::CalculateSizing(
            context, {0.0F, 0.0F, depth});
    };
    const auto nearSizing = sizingAt(0.05F);
    const auto mediumSizing = sizingAt(20.0F);
    const auto farSizing = sizingAt(500.0F);
    Require(nearSizing.Visible && mediumSizing.Visible && farSizing.Visible &&
            nearSizing.ProjectedLengthPixels <=
                TransformGizmoModel::MaximumAxisLengthPixels + 0.5F &&
            farSizing.ProjectedLengthPixels <
                mediumSizing.ProjectedLengthPixels &&
            mediumSizing.WorldLength == farSizing.WorldLength &&
            mediumSizing.WorldLength ==
                TransformGizmoModel::MinimumWorldLength,
        "Hybrid sizing must cap only the near view and shrink naturally with distance.");
    Require(nearSizing.CorrectionIterations > 0U &&
            mediumSizing.CorrectionIterations == 0U &&
            farSizing.CorrectionIterations == 0U,
        "Projection correction must only enforce the maximum pixel ceiling.");

    const auto almostNear = TransformGizmoModel::CalculateSizing(
        context, {0.0F, 0.0F, 0.001F});
    Require(almostNear.Visible && almostNear.WorldLength > 0.0F &&
            std::isfinite(almostNear.WorldLength),
        "A positive center close to the near plane must not disappear or produce NaN.");
    Require(!TransformGizmoModel::CalculateSizing(
                context, {0.0F, 0.0F, -1.0F}).Visible,
        "A center behind the camera must be hidden safely.");

    context.Projection = TransformGizmoProjection::Orthographic;
    context.Viewport = {};
    context.OrthographicWorldHeight = 32.0F;
    const Vec3 center{0.0F, 0.0F, 10.0F};
    const float orthographicNear =
        TransformGizmoModel::CalculateWorldAxisLength(context, center);
    context.CameraPosition.Z = -490.0F;
    const float orthographicFar =
        TransformGizmoModel::CalculateWorldAxisLength(context, center);
    Require(Near(orthographicNear, orthographicFar) &&
            Near(orthographicNear,
                TransformGizmoModel::MinimumWorldLength),
        "Orthographic sizing must remain bounds-relative and distance independent.");

    TransformGizmoModel model;
    context = MakeContext();
    context.ViewportHeightPixels = 0.0F;
    Require(!model.Update(context) && !model.View().Visible,
        "A zero-height viewport must be rejected safely.");
    context = MakeContext();
    context.CameraForward = {};
    Require(!model.Update(context) && !model.View().Visible,
        "A degenerate camera direction must be rejected safely.");
    context = MakeContext();
    context.VerticalFieldOfViewDegrees = 180.0F;
    Require(!model.Update(context) && !model.View().Visible,
        "An invalid FOV must be rejected safely.");

    context = MakeContext(
        SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}));
    context.Viewport = {};
    const auto minimumClamped = TransformGizmoModel::CalculateSizing(
        context, {0.0F, 0.0F, 1.0F});
    Require(minimumClamped.Visible && minimumClamped.MinimumClampApplied &&
            minimumClamped.WorldLength ==
                TransformGizmoModel::MinimumWorldLength,
        "The minimum world-size safety clamp must be finite and explicit.");
    context = MakeContext(
        SelectionBounds::FromCorners({0, 0, 0}, {63, 63, 63}));
    context.Viewport = {};
    context.CameraPosition = {0.0F, 0.0F, -1000.0F};
    const auto maximumClamped = TransformGizmoModel::CalculateSizing(
        context, {0.0F, 0.0F, 1.0F});
    Require(maximumClamped.Visible && maximumClamped.MaximumClampApplied &&
            maximumClamped.WorldLength ==
                TransformGizmoModel::MaximumWorldLength,
        "The maximum world-size safety clamp must prevent numerical explosion.");

    Require(TransformGizmoRenderPolicy::VisiblePassDepthTestEnabled &&
            !TransformGizmoRenderPolicy::VisiblePassDepthWriteEnabled &&
            TransformGizmoRenderPolicy::OccludedPassDepthTestEnabled &&
            !TransformGizmoRenderPolicy::OccludedPassDepthWriteEnabled &&
            TransformGizmoRenderPolicy::OccludedColorScale > 0.0F &&
            TransformGizmoRenderPolicy::OccludedColorScale < 1.0F &&
            TransformGizmoRenderPolicy::OccludedAlpha > 0.0F &&
            TransformGizmoRenderPolicy::OccludedAlpha < 1.0F &&
            TransformGizmoRenderPolicy::CenterScreenOverlayEnabled,
        "The gizmo needs dedicated no-write visible and attenuated occluded passes plus a stable center overlay.");
}

void TestMeasuredProjectionAcrossCamerasAndArrowGeometry()
{
    struct ProjectionCase final
    {
        Vec3 Center;
        Vec3 Forward;
        float Depth;
        float Width;
        float Height;
        float FieldOfView;
    };
    const std::array cases{
        ProjectionCase{{}, Normalize(Vec3{1.0F, 0.7F, 1.0F}),
            0.08F, 640.0F, 360.0F, 45.0F},
        ProjectionCase{{}, Normalize(Vec3{1.0F, 0.7F, 1.0F}),
            10.0F, 1280.0F, 720.0F, 45.0F},
        ProjectionCase{{}, Normalize(Vec3{1.0F, 0.7F, 1.0F}),
            500.0F, 1920.0F, 1080.0F, 45.0F},
        ProjectionCase{{1000.0F, -500.0F, 750.0F},
            Normalize(Vec3{-0.8F, 0.4F, 1.0F}),
            2000.0F, 1280.0F, 720.0F, 45.0F},
        ProjectionCase{{}, Normalize(Vec3{1.0F, 1.0F, 1.0F}),
            30.0F, 1920.0F, 1080.0F, 25.0F},
        ProjectionCase{{}, Normalize(Vec3{1.0F, 1.0F, 1.0F}),
            30.0F, 800.0F, 500.0F, 100.0F}};

    for (const ProjectionCase& fixture : cases)
    {
        TransformGizmoUpdateContext context = MakeProjectedContext(
            fixture.Center, fixture.Forward, fixture.Depth,
            fixture.Width, fixture.Height, fixture.FieldOfView);
        TransformGizmoModel model;
        Require(model.Update(context) && model.View().Visible,
            "A finite projected camera fixture must produce a gizmo.");
        for (const TransformGizmoAxisView& axis : model.View().Axes)
        {
            const float pixels = ProjectedLength(axis, context);
            if (!axis.CameraFacing && pixels >
                TransformGizmoModel::MaximumAxisLengthPixels + 1.0F)
                std::cerr << "hybrid projection diagnostic: axis="
                    << static_cast<int>(axis.Axis) << " depth="
                    << fixture.Depth << " fov=" << fixture.FieldOfView
                    << " pixels=" << pixels << " world="
                    << Length(axis.End - axis.Start) << " stored="
                    << axis.ProjectedLengthPixels << '\n';
            Require(std::isfinite(pixels) && pixels >= 0.0F &&
                    (axis.CameraFacing || pixels <=
                        TransformGizmoModel::MaximumAxisLengthPixels + 1.0F),
                "Every non-degenerate axis must respect the pixel ceiling without numerical explosion.");
            Require(axis.HasArrowHead && axis.ArrowLength > 0.0F &&
                    axis.ArrowWidth > 0.0F && axis.End != axis.ArrowBaseCenter,
                "Move must expose one finite arrow head at every axis endpoint.");
            const float projectedArrowLength = pixels *
                axis.ArrowLength / Length(axis.End - axis.Start);
            Require(std::isfinite(projectedArrowLength) &&
                    projectedArrowLength <=
                        TransformGizmoModel::MaximumArrowLengthPixels + 0.5F &&
                    projectedArrowLength <=
                        pixels * TransformGizmoModel::MaximumArrowAxisRatio +
                            0.5F,
                "Arrow heads must remain proportional and never consume the complete axis.");
            if (!axis.CameraFacing && pixels >= 15.0F)
                Require(projectedArrowLength >=
                        TransformGizmoModel::MinimumArrowLengthPixels - 0.5F,
                    "A normally projected arrow head must retain its discrete minimum size.");
        }
    }

    for (const Vec3 forward : {
             Vec3{1.0F, 0.0F, 0.0F},
             Vec3{0.0F, 1.0F, 0.0F},
             Vec3{0.0F, 0.0F, 1.0F}})
    {
        const TransformGizmoUpdateContext context =
            MakeProjectedContext({}, forward, 20.0F);
        TransformGizmoModel model;
        Require(model.Update(context),
            "A camera-facing axis must not hide the complete gizmo.");
        bool foundFacing = false;
        for (const TransformGizmoAxisView& axis : model.View().Axes)
        {
            const Vec3 direction = Normalize(axis.End - axis.Start);
            if (std::abs(Dot(direction, forward)) > 0.99F)
            {
                foundFacing = true;
                Require(axis.CameraFacing && axis.HasArrowHead &&
                        std::isfinite(axis.ArrowLength) &&
                        axis.ArrowLength <=
                            TransformGizmoModel::MaximumWorldLength,
                    "A face-on axis must use a bounded visible handle instead of exploding its world length.");
            }
            else
            {
                Require(ProjectedLength(axis, context) <=
                        TransformGizmoModel::MaximumAxisLengthPixels + 1.0F,
                    "Non-facing axes in orthogonal views must respect the pixel ceiling.");
            }
        }
        Require(foundFacing,
            "Each orthogonal camera fixture must identify its face-on axis.");
    }

    for (const ActiveVoxelTool tool : {
             ActiveVoxelTool::Rotate, ActiveVoxelTool::Scale})
    {
        TransformGizmoUpdateContext context = MakeProjectedContext(
            {}, Normalize(Vec3{1.0F, 1.0F, 1.0F}), 20.0F);
        context.ActiveTool = tool;
        TransformGizmoModel model;
        Require(model.Update(context) &&
                std::none_of(model.View().Axes.begin(),
                    model.View().Axes.end(),
                    [](const TransformGizmoAxisView& axis)
                    { return axis.HasArrowHead; }),
            "Arrow heads are exclusive to Move and must not leak into Rotate or Scale.");
    }
}

void TestSelectionShapeControlsHybridWorldSize()
{
    TransformGizmoModel model;
    float previousLength = 0.0F;
    for (const SelectionBounds bounds : {
             SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}),
             SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1}),
             SelectionBounds::FromCorners({0, 0, 0}, {15, 15, 15}),
             SelectionBounds::FromCorners({0, 0, 0}, {31, 31, 31}),
             SelectionBounds::FromCorners({0, 0, 0}, {31, 0, 31}),
             SelectionBounds::FromCorners({0, 0, 0}, {63, 0, 0})})
    {
        auto context = MakeContext(bounds);
        const Vec3 gridCenter{
            (static_cast<float>(bounds.Minimum.X + bounds.Maximum.X) + 1.0F) * 0.5F,
            (static_cast<float>(bounds.Minimum.Y + bounds.Maximum.Y) + 1.0F) * 0.5F,
            (static_cast<float>(bounds.Minimum.Z + bounds.Maximum.Z) + 1.0F) * 0.5F};
        context.CameraForward = {0.0F, 0.0F, 1.0F};
        context.CameraPosition = gridCenter - Vec3{0.0F, 0.0F, 20.0F};
        Require(model.Update(context) && model.View().Visible,
            "Every compact, large, flat or long selection must keep a visible gizmo.");
        Require(model.View().AxisLength + 0.0001F >= previousLength,
            "Larger selection bounds must not produce a smaller world-space gizmo.");
        previousLength = model.View().AxisLength;
        Require(model.View().Center == gridCenter,
            "Every selection shape must retain its exact spatial center.");
    }
    const auto unitContext = MakeContext(
        SelectionBounds::FromCorners({0, 0, 0}, {0, 0, 0}));
    auto maximumContext = MakeContext(
        SelectionBounds::FromCorners({0, 0, 0}, {63, 63, 63}));
    maximumContext.CameraPosition = {32.0F, 32.0F, -1000.0F};
    Require(Near(TransformGizmoModel::CalculateSizing(
                     unitContext, {0.5F, 0.5F, 0.5F}).UnclampedWorldLength,
                TransformGizmoModel::SelectionRelativeFactor) &&
            Near(TransformGizmoModel::CalculateSizing(
                     maximumContext, {32.0F, 32.0F, 32.0F}).WorldLength,
                TransformGizmoModel::MaximumWorldLength),
        "The hybrid world size must use the selection factor and explicit world clamps.");
}

void TestImmutableRenderViewAndConstantPrimitives()
{
    SelectionService selection;
    selection.SetDocumentGeneration(7U);
    const std::vector<VoxelPosition> voxels{{2, 3, 4}, {8, 6, 5}};
    Require(selection.ApplySortedVolume(voxels,
            SelectionBounds::FromCorners({2, 3, 4}, {8, 6, 5}),
            SelectionMode::Replace),
        "Selection fixture setup failed.");
    const auto selectionBefore = std::vector<VoxelPosition>(
        selection.Voxels().begin(), selection.Voxels().end());
    const SelectionBounds boundsBefore = selection.EditableBounds();
    TransformGizmoModel model;
    auto context = MakeContext(boundsBefore);
    Require(model.Update(context), "A valid render view was not produced.");
    const TransformGizmoView first = model.View();
    Require(first.Axes.size() == TransformGizmoModel::AxisPrimitiveCount &&
            TransformGizmoModel::TotalPrimitiveCount == 7U &&
            first.Axes[0].Axis == TransformGizmoAxis::X &&
            first.Axes[1].Axis == TransformGizmoAxis::Y &&
            first.Axes[2].Axis == TransformGizmoAxis::Z,
        "The renderer view must contain exactly three ordered axes.");
    for (const TransformGizmoAxisView& axis : first.Axes)
        Require(axis.Start == first.Center &&
                Near(Length(axis.End - axis.Start), first.AxisLength),
            "Axes must share one origin and one length.");
    const Vec3 xDelta = first.Axes[0].End - first.Axes[0].Start;
    const Vec3 yDelta = first.Axes[1].End - first.Axes[1].Start;
    const Vec3 zDelta = first.Axes[2].End - first.Axes[2].Start;
    Require(Near(xDelta.X, first.AxisLength) && Near(xDelta.Y, 0.0F) &&
            Near(xDelta.Z, 0.0F) && Near(yDelta.X, 0.0F) &&
            Near(yDelta.Y, first.AxisLength) && Near(yDelta.Z, 0.0F) &&
            Near(zDelta.X, 0.0F) && Near(zDelta.Y, 0.0F) &&
            Near(zDelta.Z, first.AxisLength) &&
            first.Axes[0].Color[0] > first.Axes[0].Color[1] &&
            first.Axes[1].Color[1] > first.Axes[1].Color[0] &&
            first.Axes[2].Color[2] > first.Axes[2].Color[0],
        "Axes must be orthogonal and retain X red, Y green and Z blue.");
    Require(!model.Update(context) && model.View() == first,
        "An unchanged frame must not rebuild the immutable view.");
    Require(selection.EditableBounds() == boundsBefore &&
            std::equal(selection.Voxels().begin(), selection.Voxels().end(),
                selectionBefore.begin()),
        "Gizmo evaluation must not mutate selection or its bounds.");
}

void TestNoDocumentOrHistoryMutation()
{
    using namespace VoxelForge;
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({{8U, 8U, 8U}, {
        {2U, 3U, 4U, 7U},
        {5U, 6U, 1U, 11U}}});
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "transform-gizmo-memory.vox", "gizmo-asset");
    Require(loaded.Succeeded(),
        "Unable to build the non-mutation document fixture.");
    Asset::Voxel::VoxelDocument document = std::move(*loaded.Document);
    VoxelEditHistory history;

    const std::uint64_t revisionBefore = document.GetRevision();
    const bool dirtyBefore = document.IsDirty();
    const std::uint64_t voxelCountBefore = document.GetVoxelCount();
    const auto boundsBefore = document.GetBounds();
    const auto firstVoxelBefore = document.GetVoxel({2, 3, 4});
    const std::size_t undoCountBefore = history.UndoCount();
    const std::size_t redoCountBefore = history.RedoCount();
    const std::size_t historyMemoryBefore = history.EstimatedMemory();
    const bool savedStateBefore = history.IsAtSavedState();

    TransformGizmoModel model;
    const auto context = MakeContext(
        SelectionBounds::FromCorners({2, 3, 4}, {5, 6, 4}),
        ActiveVoxelTool::Scale);
    Require(model.Update(context) && model.View().Visible,
        "The non-mutation fixture did not produce a visible gizmo.");
    Require(document.GetRevision() == revisionBefore &&
            document.IsDirty() == dirtyBefore &&
            document.GetVoxelCount() == voxelCountBefore &&
            document.GetBounds() == boundsBefore &&
            document.GetVoxel({2, 3, 4}) == firstVoxelBefore,
        "Gizmo evaluation must not mutate document data, revision or dirty.");
    Require(history.UndoCount() == undoCountBefore &&
            history.RedoCount() == redoCountBefore &&
            history.EstimatedMemory() == historyMemoryBefore &&
            history.IsAtSavedState() == savedStateBefore &&
            !history.CanUndo() && !history.CanRedo(),
        "Gizmo evaluation must not create or modify edit history.");
}

void TestAxisFilteringForRestrictedTransforms()
{
    TransformGizmoModel model;
    TransformGizmoUpdateContext context = MakeProjectedContext(
        {}, Normalize(Vec3{1.0F, 1.0F, 1.0F}), 20.0F);
    context.ActiveTool = ActiveVoxelTool::Rotate;
    context.AxisEnabled = {{false, true, false}};
    Require(model.Update(context) && model.View().Visible &&
                !model.View().Axes[0].Enabled &&
                model.View().Axes[1].Enabled &&
                !model.View().Axes[2].Enabled &&
                !model.View().Axes[0].HasRotationRing &&
                model.View().Axes[1].HasRotationRing &&
                !model.View().Axes[2].HasRotationRing,
        "A restricted transform must expose only its supported gizmo axis.");
}
}

int main()
{
    try
    {
        TestVisibilityModesAndStateMachine();
        TestExactSpatialCentersAndInvalidation();
        TestHybridScaleAndDegenerateInputs();
        TestMeasuredProjectionAcrossCamerasAndArrowGeometry();
        TestSelectionShapeControlsHybridWorldSize();
        TestImmutableRenderViewAndConstantPrimitives();
        TestNoDocumentOrHistoryMutation();
        TestAxisFilteringForRestrictedTransforms();
    }
    catch (const std::exception& error)
    {
        std::cerr << "TransformGizmoTests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "TransformGizmoTests passed\n";
    return EXIT_SUCCESS;
}
