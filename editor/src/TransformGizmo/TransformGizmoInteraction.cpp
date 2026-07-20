#include "TransformGizmoInteraction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace VoxelForge::Editor
{
namespace
{
constexpr float Epsilon = 1.0e-6F;

[[nodiscard]] bool IsFinite(const Vec2 value) noexcept
{
    return std::isfinite(value.X) && std::isfinite(value.Y);
}

[[nodiscard]] float DistanceToSegmentSquared(
    const Vec2 point, const Vec2 start, const Vec2 end) noexcept
{
    const float x = end.X - start.X;
    const float y = end.Y - start.Y;
    const float lengthSquared = x * x + y * y;
    if (!std::isfinite(lengthSquared) || lengthSquared <= Epsilon)
    {
        const float dx = point.X - start.X;
        const float dy = point.Y - start.Y;
        return dx * dx + dy * dy;
    }
    const float projection = std::clamp(
        ((point.X - start.X) * x + (point.Y - start.Y) * y) /
            lengthSquared,
        0.0F, 1.0F);
    const float dx = point.X - (start.X + projection * x);
    const float dy = point.Y - (start.Y + projection * y);
    return dx * dx + dy * dy;
}

[[nodiscard]] float SignedTriangleArea(
    const Vec2 a, const Vec2 b, const Vec2 c) noexcept
{
    return (b.X - a.X) * (c.Y - a.Y) -
        (b.Y - a.Y) * (c.X - a.X);
}

[[nodiscard]] bool PointInTriangle(
    const Vec2 point, const Vec2 a, const Vec2 b, const Vec2 c) noexcept
{
    const float first = SignedTriangleArea(a, b, point);
    const float second = SignedTriangleArea(b, c, point);
    const float third = SignedTriangleArea(c, a, point);
    const bool negative = first < 0.0F || second < 0.0F || third < 0.0F;
    const bool positive = first > 0.0F || second > 0.0F || third > 0.0F;
    return !(negative && positive);
}

[[nodiscard]] float DistanceToArrowSquared(
    const Vec2 point,
    const Vec2 tip,
    const std::array<Vec2, 4U>& base) noexcept
{
    for (std::size_t index = 0U; index < base.size(); ++index)
    {
        const Vec2 next = base[(index + 1U) % base.size()];
        if (PointInTriangle(point, tip, base[index], next)) return 0.0F;
    }
    float distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0U; index < base.size(); ++index)
    {
        const Vec2 next = base[(index + 1U) % base.size()];
        distance = std::min(distance,
            DistanceToSegmentSquared(point, tip, base[index]));
        distance = std::min(distance,
            DistanceToSegmentSquared(point, base[index], next));
    }
    return distance;
}

[[nodiscard]] std::int32_t SnapToGrid(const float value) noexcept
{
    if (!std::isfinite(value)) return 0;
    const double bounded = std::clamp(
        static_cast<double>(value),
        static_cast<double>(std::numeric_limits<std::int32_t>::min()),
        static_cast<double>(std::numeric_limits<std::int32_t>::max()));
    return static_cast<std::int32_t>(std::llround(bounded));
}
}

TransformGizmoAxis TransformGizmoInteraction::UpdateHover(
    const TransformGizmoView& view,
    const TransformGizmoPointerInput& input) noexcept
{
    if (IsDragging()) return lockedAxis_;
    hoveredAxis_ = TransformGizmoAxis::None;
    state_ = TransformGizmoInteractionState::Idle;
    if (!view.Visible || view.Mode != TransformGizmoMode::Move ||
        !IsFinite(input.ScreenPosition) || input.Viewport.Width <= 0.0F ||
        input.Viewport.Height <= 0.0F || !IsFinite(input.ViewProjection))
        return hoveredAxis_;

    float bestDistance = PickTolerancePixels * PickTolerancePixels;
    for (const TransformGizmoAxisView& axis : view.Axes)
    {
        const auto start = TransformGizmoModel::ProjectWorldToScreen(
            axis.Start, input.Viewport, input.ViewProjection);
        const auto end = TransformGizmoModel::ProjectWorldToScreen(
            axis.End, input.Viewport, input.ViewProjection);
        if (!start || !end) continue;
        float distance = DistanceToSegmentSquared(
            input.ScreenPosition, *start, *end);
        if (axis.HasArrowHead)
        {
            std::array<Vec2, 4U> base{};
            bool projected = true;
            for (std::size_t index = 0U; index < base.size(); ++index)
            {
                const auto corner = TransformGizmoModel::ProjectWorldToScreen(
                    axis.ArrowBaseCorners[index], input.Viewport,
                    input.ViewProjection);
                if (!corner)
                {
                    projected = false;
                    break;
                }
                base[index] = *corner;
            }
            if (projected)
                distance = std::min(distance,
                    DistanceToArrowSquared(
                        input.ScreenPosition, *end, base));
            if (axis.CameraFacing)
            {
                const float dx = input.ScreenPosition.X - end->X;
                const float dy = input.ScreenPosition.Y - end->Y;
                const float radius = CameraFacingHandleRadiusPixels;
                if (dx * dx + dy * dy <= radius * radius)
                    distance = 0.0F;
            }
        }
        // Strictly-less preserves the stable X/Y/Z array order on ties.
        if (distance < bestDistance)
        {
            bestDistance = distance;
            hoveredAxis_ = axis.Axis;
        }
    }
    if (hoveredAxis_ != TransformGizmoAxis::None)
        state_ = TransformGizmoInteractionState::Hover;
    return hoveredAxis_;
}

bool TransformGizmoInteraction::BeginDrag(
    const TransformGizmoView& view,
    const TransformGizmoAxis axis,
    const TransformGizmoPointerInput& input,
    const std::uint64_t documentGeneration,
    const SelectionBounds selectionBounds) noexcept
{
    if (IsDragging() || !view.Visible || view.Mode != TransformGizmoMode::Move ||
        axis == TransformGizmoAxis::None || axis != hoveredAxis_ ||
        documentGeneration == 0U || !selectionBounds.Valid ||
        !IsFinite(input.ScreenPosition) || !std::isfinite(view.AxisLength) ||
        view.AxisLength <= 0.0F)
        return false;

    const Vec3 direction = AxisVector(axis);
    const auto centerScreen = TransformGizmoModel::ProjectWorldToScreen(
        view.Center, input.Viewport, input.ViewProjection);
    const auto axisView = std::find_if(
        view.Axes.begin(), view.Axes.end(),
        [axis](const TransformGizmoAxisView& candidate)
        { return candidate.Axis == axis; });
    if (axisView == view.Axes.end()) return false;
    const float worldAxisLength = Length(axisView->End - axisView->Start);
    const auto endScreen = TransformGizmoModel::ProjectWorldToScreen(
        axisView->End, input.Viewport, input.ViewProjection);
    if (!centerScreen || !endScreen) return false;

    const Vec2 projected{
        endScreen->X - centerScreen->X,
        endScreen->Y - centerScreen->Y};
    const float projectedLength = std::sqrt(
        projected.X * projected.X + projected.Y * projected.Y);
    screenAxisDirection_ = projectedLength >= MinimumProjectedAxisPixels
        ? Vec2{projected.X / projectedLength, projected.Y / projectedLength}
        : Vec2{0.0F, -1.0F};
    const float stablePixelLength = projectedLength >= MinimumProjectedAxisPixels
        ? projectedLength : CameraFacingHandleRadiusPixels * 2.0F;
    worldUnitsPerPixel_ = worldAxisLength / stablePixelLength;
    if (!std::isfinite(worldUnitsPerPixel_) || worldUnitsPerPixel_ <= 0.0F)
        return false;

    state_ = TransformGizmoInteractionState::Dragging;
    lockedAxis_ = axis;
    axisOrigin_ = view.Center;
    axisDirection_ = direction;
    pointerStart_ = input.ScreenPosition;
    documentGeneration_ = documentGeneration;
    selectionBounds_ = selectionBounds;
    delta_ = {};
    rayAnchorParameter_ = input.Ray
        ? ClosestAxisParameter(*input.Ray, axisOrigin_, axisDirection_)
        : std::nullopt;
    return true;
}

bool TransformGizmoInteraction::UpdateDrag(
    const TransformGizmoPointerInput& input) noexcept
{
    if (!IsDragging() || !IsFinite(input.ScreenPosition)) return false;
    float worldDelta = ScreenFallbackParameter(input.ScreenPosition);
    if (input.Ray && rayAnchorParameter_)
    {
        if (const auto parameter = ClosestAxisParameter(
                *input.Ray, axisOrigin_, axisDirection_))
            worldDelta = *parameter - *rayAnchorParameter_;
    }
    const std::int32_t snapped = SnapToGrid(worldDelta);
    Asset::Voxel::VoxelPosition next{};
    if (lockedAxis_ == TransformGizmoAxis::X) next.X = snapped;
    else if (lockedAxis_ == TransformGizmoAxis::Y) next.Y = snapped;
    else if (lockedAxis_ == TransformGizmoAxis::Z) next.Z = snapped;
    if (next == delta_) return false;
    delta_ = next;
    return true;
}

TransformGizmoDragRelease TransformGizmoInteraction::EndDrag() noexcept
{
    TransformGizmoDragRelease release;
    if (IsDragging())
    {
        release.Axis = lockedAxis_;
        release.Delta = delta_;
        release.WasDragging = true;
    }
    ClearDrag();
    state_ = hoveredAxis_ == TransformGizmoAxis::None
        ? TransformGizmoInteractionState::Idle
        : TransformGizmoInteractionState::Hover;
    return release;
}

bool TransformGizmoInteraction::Validate(
    const std::uint64_t documentGeneration,
    const SelectionBounds selectionBounds,
    const bool moveToolActive) noexcept
{
    if (!IsDragging()) return true;
    if (moveToolActive && documentGeneration_ == documentGeneration &&
        selectionBounds_ == selectionBounds)
        return true;
    static_cast<void>(Cancel());
    return false;
}

bool TransformGizmoInteraction::Cancel() noexcept
{
    const bool changed = IsDragging() ||
        hoveredAxis_ != TransformGizmoAxis::None;
    Reset();
    return changed;
}

void TransformGizmoInteraction::Reset() noexcept
{
    hoveredAxis_ = TransformGizmoAxis::None;
    state_ = TransformGizmoInteractionState::Idle;
    ClearDrag();
}

bool TransformGizmoInteraction::IsDragging() const noexcept
{
    return state_ == TransformGizmoInteractionState::Dragging;
}

TransformGizmoAxis TransformGizmoInteraction::HoveredAxis() const noexcept
{
    return hoveredAxis_;
}

TransformGizmoAxis TransformGizmoInteraction::LockedAxis() const noexcept
{
    return lockedAxis_;
}

Asset::Voxel::VoxelPosition TransformGizmoInteraction::Delta() const noexcept
{
    return delta_;
}

TransformGizmoInteractionState TransformGizmoInteraction::State() const noexcept
{
    return state_;
}

Vec3 TransformGizmoInteraction::AxisVector(
    const TransformGizmoAxis axis) noexcept
{
    if (axis == TransformGizmoAxis::X) return {1.0F, 0.0F, 0.0F};
    if (axis == TransformGizmoAxis::Y) return {0.0F, 1.0F, 0.0F};
    if (axis == TransformGizmoAxis::Z) return {0.0F, 0.0F, 1.0F};
    return {};
}

std::optional<float> TransformGizmoInteraction::ClosestAxisParameter(
    const VoxelRay& ray,
    const Vec3 axisOrigin,
    const Vec3 axisDirection) noexcept
{
    const Vec3 direction = Normalize(ray.Direction);
    if (Length(direction) <= Epsilon || !IsFinite(ray.Origin) ||
        !IsFinite(axisOrigin) || !IsFinite(axisDirection))
        return std::nullopt;
    const float parallel = Dot(axisDirection, direction);
    const float denominator = 1.0F - parallel * parallel;
    if (!std::isfinite(denominator) || denominator <= 1.0e-4F)
        return std::nullopt;
    const Vec3 offset = axisOrigin - ray.Origin;
    const float parameter =
        (parallel * Dot(offset, direction) - Dot(offset, axisDirection)) /
        denominator;
    return std::isfinite(parameter)
        ? std::optional<float>(parameter) : std::nullopt;
}

float TransformGizmoInteraction::ScreenFallbackParameter(
    const Vec2 pointer) const noexcept
{
    const Vec2 delta{pointer.X - pointerStart_.X, pointer.Y - pointerStart_.Y};
    const float pixels = delta.X * screenAxisDirection_.X +
        delta.Y * screenAxisDirection_.Y;
    const float result = pixels * worldUnitsPerPixel_;
    return std::isfinite(result) ? result : 0.0F;
}

void TransformGizmoInteraction::ClearDrag() noexcept
{
    lockedAxis_ = TransformGizmoAxis::None;
    axisOrigin_ = {};
    axisDirection_ = {};
    pointerStart_ = {};
    screenAxisDirection_ = {0.0F, -1.0F};
    worldUnitsPerPixel_ = 0.0F;
    rayAnchorParameter_.reset();
    delta_ = {};
    documentGeneration_ = 0U;
    selectionBounds_ = {};
}

} // namespace VoxelForge::Editor
