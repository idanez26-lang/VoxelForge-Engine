#include "TransformGizmoModel.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<float, 4U> XAxisColor{0.94F, 0.20F, 0.18F, 1.0F};
constexpr std::array<float, 4U> YAxisColor{0.24F, 0.86F, 0.32F, 1.0F};
constexpr std::array<float, 4U> ZAxisColor{0.20F, 0.46F, 1.0F, 1.0F};
constexpr float ProjectionTolerancePixels = 0.25F;

[[nodiscard]] TransformGizmoView HiddenView() noexcept
{
    return {};
}

[[nodiscard]] Vec3 AxisVector(const TransformGizmoAxis axis) noexcept
{
    if (axis == TransformGizmoAxis::X) return {1.0F, 0.0F, 0.0F};
    if (axis == TransformGizmoAxis::Y) return {0.0F, 1.0F, 0.0F};
    if (axis == TransformGizmoAxis::Z) return {0.0F, 0.0F, 1.0F};
    return {};
}

[[nodiscard]] std::array<Vec3, 4U> ArrowBaseCorners(
    const TransformGizmoAxis axis,
    const Vec3 center,
    const float halfWidth) noexcept
{
    if (axis == TransformGizmoAxis::X)
        return {{{center.X, center.Y - halfWidth, center.Z - halfWidth},
                 {center.X, center.Y + halfWidth, center.Z - halfWidth},
                 {center.X, center.Y + halfWidth, center.Z + halfWidth},
                 {center.X, center.Y - halfWidth, center.Z + halfWidth}}};
    if (axis == TransformGizmoAxis::Y)
        return {{{center.X - halfWidth, center.Y, center.Z - halfWidth},
                 {center.X + halfWidth, center.Y, center.Z - halfWidth},
                 {center.X + halfWidth, center.Y, center.Z + halfWidth},
                 {center.X - halfWidth, center.Y, center.Z + halfWidth}}};
    return {{{center.X - halfWidth, center.Y - halfWidth, center.Z},
             {center.X + halfWidth, center.Y - halfWidth, center.Z},
             {center.X + halfWidth, center.Y + halfWidth, center.Z},
             {center.X - halfWidth, center.Y + halfWidth, center.Z}}};
}
}

TransformGizmoMode TransformGizmoModel::ModeForTool(
    const ActiveVoxelTool tool) noexcept
{
    switch (tool)
    {
    case ActiveVoxelTool::Move: return TransformGizmoMode::Move;
    case ActiveVoxelTool::Rotate: return TransformGizmoMode::Rotate;
    case ActiveVoxelTool::Scale: return TransformGizmoMode::Scale;
    default: return TransformGizmoMode::None;
    }
}

float TransformGizmoModel::CalculateWorldAxisLength(
    const TransformGizmoUpdateContext& context,
    const Vec3 worldCenter) noexcept
{
    return CalculateSizing(context, worldCenter).WorldLength;
}

TransformGizmoSizingResult TransformGizmoModel::CalculateSizing(
    const TransformGizmoUpdateContext& context,
    const Vec3 worldCenter,
    const TransformGizmoAxis axis) noexcept
{
    TransformGizmoSizingResult result;
    if (!IsFinite(worldCenter) || !IsFinite(context.CameraPosition) ||
        !IsFinite(context.CameraForward) ||
        !std::isfinite(context.ViewportHeightPixels) ||
        context.ViewportHeightPixels <= 0.0F)
        return result;

    const Vec3 forward = Normalize(context.CameraForward);
    if (Length(forward) <= 0.00001F) return result;
    result.CameraDepth = Dot(worldCenter - context.CameraPosition, forward);
    if (!std::isfinite(result.CameraDepth) ||
        result.CameraDepth <= MinimumPositiveDepth)
        return result;

    if (!context.Bounds.Valid) return result;
    const float extentX = static_cast<float>(
        context.Bounds.Maximum.X - context.Bounds.Minimum.X + 1);
    const float extentY = static_cast<float>(
        context.Bounds.Maximum.Y - context.Bounds.Minimum.Y + 1);
    const float extentZ = static_cast<float>(
        context.Bounds.Maximum.Z - context.Bounds.Minimum.Z + 1);
    result.SelectionMaximumExtent = std::max({extentX, extentY, extentZ});
    result.UnclampedWorldLength =
        result.SelectionMaximumExtent * SelectionRelativeFactor;
    if (!std::isfinite(result.UnclampedWorldLength) ||
        result.UnclampedWorldLength <= 0.0F)
        return result;
    result.WorldLength = std::clamp(
        result.UnclampedWorldLength, MinimumWorldLength, MaximumWorldLength);
    result.MinimumClampApplied = result.WorldLength >
        result.UnclampedWorldLength;
    result.MaximumClampApplied = result.WorldLength <
        result.UnclampedWorldLength;
    const Vec3 direction = AxisVector(axis);
    const auto measureProjectedLength = [&](const float worldLength)
        -> std::optional<float>
    {
        const auto start = ProjectWorldToScreen(
            worldCenter, context.Viewport, context.ViewProjection);
        const auto end = ProjectWorldToScreen(
            worldCenter + direction * worldLength,
            context.Viewport, context.ViewProjection);
        if (!start || !end) return std::nullopt;
        const float dx = end->X - start->X;
        const float dy = end->Y - start->Y;
        const float pixels = std::sqrt(dx * dx + dy * dy);
        return std::isfinite(pixels)
            ? std::optional<float>(pixels) : std::nullopt;
    };

    const bool hasFinalProjection =
        context.Viewport.Width > 0.0F && context.Viewport.Height > 0.0F &&
        IsFinite(context.ViewProjection) &&
        Length(direction) > 0.0F;
    if (hasFinalProjection)
    {
        const auto initialPixels = measureProjectedLength(result.WorldLength);
        const float alignment = std::clamp(
            std::abs(Dot(direction, forward)), 0.0F, 1.0F);
        const float viewSine = std::sqrt(std::max(
            0.0F, 1.0F - alignment * alignment));
        if (!initialPixels || viewSine < MinimumAxisViewSine)
        {
            result.CameraFacing = true;
            result.ProjectedLengthPixels = initialPixels.value_or(0.0F);
        }
        else
        {
            result.ProjectedLengthPixels = *initialPixels;
            for (std::uint8_t iteration = 0U; iteration < 4U; ++iteration)
            {
                if (result.ProjectedLengthPixels <=
                    MaximumAxisLengthPixels + ProjectionTolerancePixels)
                    break;
                const float requestedCorrection =
                    AxisCeilingTargetPixels / result.ProjectedLengthPixels;
                const float correction = std::clamp(
                    requestedCorrection,
                    MinimumCorrectionFactor,
                    MaximumCorrectionFactor);
                result.CorrectionMinimumClampApplied |=
                    correction > requestedCorrection;
                result.CorrectionMaximumClampApplied |=
                    correction < requestedCorrection;
                const float unbounded = result.WorldLength * correction;
                result.WorldLength = std::clamp(
                    unbounded, MinimumScreenCappedWorldLength,
                    MaximumWorldLength);
                result.MinimumClampApplied |= result.WorldLength > unbounded;
                result.MaximumClampApplied |= result.WorldLength < unbounded;
                ++result.CorrectionIterations;
                const auto correctedPixels =
                    measureProjectedLength(result.WorldLength);
                if (!correctedPixels || *correctedPixels <= 0.0F)
                    return {};
                result.ProjectedLengthPixels = *correctedPixels;
            }
        }
    }
    else if (context.Projection == TransformGizmoProjection::Orthographic)
    {
        if (!std::isfinite(context.OrthographicWorldHeight) ||
            context.OrthographicWorldHeight <= 0.0F)
            return {};
        result.ProjectedLengthPixels = result.WorldLength *
            context.ViewportHeightPixels / context.OrthographicWorldHeight;
    }
    else
    {
        if (!std::isfinite(context.VerticalFieldOfViewDegrees) ||
            context.VerticalFieldOfViewDegrees <= 1.0F ||
            context.VerticalFieldOfViewDegrees >= 179.0F)
            return {};
        const float halfFov = DegreesToRadians(
            context.VerticalFieldOfViewDegrees) * 0.5F;
        result.ProjectedLengthPixels = result.WorldLength *
            context.ViewportHeightPixels /
            (result.CameraDepth * 2.0F * std::tan(halfFov));
        if (result.ProjectedLengthPixels > MaximumAxisLengthPixels)
        {
            const float unbounded = result.WorldLength *
                AxisCeilingTargetPixels / result.ProjectedLengthPixels;
            result.WorldLength = std::clamp(
                unbounded, MinimumScreenCappedWorldLength,
                MaximumWorldLength);
            result.ProjectedLengthPixels = result.WorldLength *
                context.ViewportHeightPixels /
                (result.CameraDepth * 2.0F * std::tan(halfFov));
            result.MaximumClampApplied = true;
            result.CorrectionIterations = 1U;
        }
    }
    result.Visible = std::isfinite(result.ProjectedLengthPixels) &&
        (result.ProjectedLengthPixels > 0.0F || result.CameraFacing);
    if (!result.Visible) result.WorldLength = 0.0F;
    return result;
}

std::optional<Vec2> TransformGizmoModel::ProjectWorldToScreen(
    const Vec3 worldPosition,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept
{
    if (viewport.Width <= 0.0F || viewport.Height <= 0.0F ||
        !IsFinite(viewProjection) || !IsFinite(worldPosition))
        return std::nullopt;
    const Vec3 projected = TransformPoint(viewProjection, worldPosition);
    if (!IsFinite(projected)) return std::nullopt;
    const Vec2 screen{
        viewport.X + (projected.X * 0.5F + 0.5F) * viewport.Width,
        viewport.Y + (0.5F - projected.Y * 0.5F) * viewport.Height};
    if (!std::isfinite(screen.X) || !std::isfinite(screen.Y))
        return std::nullopt;
    return screen;
}

std::string_view TransformGizmoModel::ContextHelpFor(
    const TransformGizmoInteractionState state,
    const TransformGizmoAxis axis) noexcept
{
    if (state == TransformGizmoInteractionState::Dragging)
    {
        if (axis == TransformGizmoAxis::X)
            return "Moving on X — Release to apply — Esc to cancel";
        if (axis == TransformGizmoAxis::Y)
            return "Moving on Y — Release to apply — Esc to cancel";
        if (axis == TransformGizmoAxis::Z)
            return "Moving on Z — Release to apply — Esc to cancel";
    }
    if (state == TransformGizmoInteractionState::Hover)
    {
        if (axis == TransformGizmoAxis::X) return "Move X";
        if (axis == TransformGizmoAxis::Y) return "Move Y";
        if (axis == TransformGizmoAxis::Z) return "Move Z";
    }
    return {};
}

bool TransformGizmoModel::Update(
    const TransformGizmoUpdateContext& context) noexcept
{
    const TransformGizmoMode mode = ModeForTool(context.ActiveTool);
    if (!context.DocumentActive || context.SelectionEmpty || context.Closing ||
        context.ActiveDocumentGeneration == 0U ||
        context.SelectionDocumentGeneration !=
            context.ActiveDocumentGeneration ||
        !context.Bounds.Valid || mode == TransformGizmoMode::None)
    {
        const TransformGizmoView hidden = HiddenView();
        if (view_ == hidden) return false;
        view_ = hidden;
        return true;
    }

    // Voxel bounds are inclusive integer cell coordinates. The spatial box is
    // [minimum, maximum + 1], then translated by the same model center used by
    // the viewport mesh and every existing selection overlay.
    const Vec3 gridCenter{
        (static_cast<float>(context.Bounds.Minimum.X) +
         static_cast<float>(context.Bounds.Maximum.X) + 1.0F) * 0.5F,
        (static_cast<float>(context.Bounds.Minimum.Y) +
         static_cast<float>(context.Bounds.Maximum.Y) + 1.0F) * 0.5F,
        (static_cast<float>(context.Bounds.Minimum.Z) +
         static_cast<float>(context.Bounds.Maximum.Z) + 1.0F) * 0.5F};
    const Vec3 worldCenter = gridCenter - context.ModelCenter;
    const std::array<TransformGizmoSizingResult, 3U> sizings{{
        CalculateSizing(context, worldCenter, TransformGizmoAxis::X),
        CalculateSizing(context, worldCenter, TransformGizmoAxis::Y),
        CalculateSizing(context, worldCenter, TransformGizmoAxis::Z)}};
    if (!sizings[0].Visible || !sizings[1].Visible || !sizings[2].Visible)
    {
        const TransformGizmoView hidden = HiddenView();
        if (view_ == hidden) return false;
        view_ = hidden;
        return true;
    }

    TransformGizmoView next;
    next.Visible = true;
    next.Mode = mode;
    next.State = mode == TransformGizmoMode::Move
        ? context.InteractionState : TransformGizmoInteractionState::Idle;
    next.ActiveAxis = mode == TransformGizmoMode::Move
        ? context.ActiveAxis : TransformGizmoAxis::None;
    next.Center = worldCenter;
    next.AxisLength = std::max({
        sizings[0].WorldLength,
        sizings[1].WorldLength,
        sizings[2].WorldLength});
    const float representativeLength =
        (sizings[0].WorldLength + sizings[1].WorldLength +
         sizings[2].WorldLength) / 3.0F;
    const float representativePixels = std::max(1.0F,
        (sizings[0].ProjectedLengthPixels +
         sizings[1].ProjectedLengthPixels +
         sizings[2].ProjectedLengthPixels) / 3.0F);
    const float representativeWorldPerPixel =
        representativeLength / representativePixels;
    next.AxisThickness = std::min(
        representativeWorldPerPixel * NormalAxisThicknessPixels,
        representativeLength * 0.12F);
    next.CenterRadius = std::min(
        representativeWorldPerPixel * MinimumCenterPixels,
        representativeLength * 0.24F);
    const auto styled = [&next](const TransformGizmoAxis axis,
                                const std::array<float, 4U> color)
    {
        std::array<float, 4U> result = color;
        if (next.State == TransformGizmoInteractionState::Dragging &&
            axis != next.ActiveAxis)
        {
            result[0] *= InactiveDragColorScale;
            result[1] *= InactiveDragColorScale;
            result[2] *= InactiveDragColorScale;
        }
        else if (axis == next.ActiveAxis)
        {
            result[0] += (1.0F - result[0]) * HoverColorBlend;
            result[1] += (1.0F - result[1]) * HoverColorBlend;
            result[2] += (1.0F - result[2]) * HoverColorBlend;
        }
        return result;
    };
    const auto makeAxis = [&next, &sizings, &styled, mode, worldCenter](
        const std::size_t index,
        const TransformGizmoAxis axis,
        const std::array<float, 4U> color)
    {
        const TransformGizmoSizingResult& sizing = sizings[index];
        const Vec3 direction = AxisVector(axis);
        TransformGizmoAxisView result;
        result.Axis = axis;
        result.Start = worldCenter;
        result.End = worldCenter + direction * sizing.WorldLength;
        result.Color = styled(axis, color);
        const float projectedPixels = std::max(
            sizing.ProjectedLengthPixels, 1.0F);
        const float worldPerPixel = sizing.WorldLength / projectedPixels;
        result.Thickness = std::min(
            worldPerPixel * (axis == next.ActiveAxis
                ? ActiveAxisThicknessPixels : NormalAxisThicknessPixels),
            sizing.WorldLength * 0.12F);
        result.ProjectedLengthPixels = sizing.ProjectedLengthPixels;
        result.CameraFacing = sizing.CameraFacing;
        result.HasArrowHead = mode == TransformGizmoMode::Move;
        if (result.HasArrowHead)
        {
            const float requestedArrowPixels = std::clamp(
                sizing.ProjectedLengthPixels * ArrowLengthRatio,
                MinimumArrowLengthPixels, MaximumArrowLengthPixels);
            const float arrowRatio = sizing.ProjectedLengthPixels > 0.001F
                ? std::min(MaximumArrowAxisRatio,
                    requestedArrowPixels / sizing.ProjectedLengthPixels)
                : MaximumArrowAxisRatio;
            const float requestedWidthPixels = std::clamp(
                requestedArrowPixels * 0.55F,
                MinimumArrowWidthPixels, MaximumArrowWidthPixels);
            result.ArrowLength = sizing.WorldLength * arrowRatio;
            result.ArrowWidth = std::min({
                worldPerPixel * requestedWidthPixels,
                sizing.WorldLength * 0.55F,
                result.ArrowLength * 0.75F});
            result.ArrowBaseCenter = result.End -
                direction * result.ArrowLength;
            result.ArrowBaseCorners = ArrowBaseCorners(
                axis, result.ArrowBaseCenter, result.ArrowWidth * 0.5F);
        }
        return result;
    };
    next.Axes = {{
        makeAxis(0U, TransformGizmoAxis::X, XAxisColor),
        makeAxis(1U, TransformGizmoAxis::Y, YAxisColor),
        makeAxis(2U, TransformGizmoAxis::Z, ZAxisColor)}};
    if (view_ == next) return false;
    view_ = next;
    return true;
}

void TransformGizmoModel::Reset() noexcept
{
    view_ = HiddenView();
}

const TransformGizmoView& TransformGizmoModel::View() const noexcept
{
    return view_;
}

} // namespace VoxelForge::Editor
