#include "SelectionHandleModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VoxelForge::Editor
{
namespace
{
constexpr float MinimumProjectedAxisLength = 0.5F;
constexpr Vec2 FallbackScreenAxis{0.0F, -12.0F};

[[nodiscard]] Vec3 PositiveAxis(const SelectionFace face) noexcept
{
    switch (face)
    {
    case SelectionFace::XMinimum:
    case SelectionFace::XMaximum: return {1.0F, 0.0F, 0.0F};
    case SelectionFace::YMinimum:
    case SelectionFace::YMaximum: return {0.0F, 1.0F, 0.0F};
    case SelectionFace::ZMinimum:
    case SelectionFace::ZMaximum: return {0.0F, 0.0F, 1.0F};
    case SelectionFace::None:
    default: return {};
    }
}

struct ProjectedPoint final
{
    Vec2 Screen{};
    float Depth = 0.0F;
    bool Visible = false;
};

[[nodiscard]] ProjectedPoint ProjectPoint(
    const Vec3 worldPosition,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept
{
    const Vec3 normalized = TransformPoint(viewProjection, worldPosition);
    if (!IsFinite(normalized) || viewport.Width <= 0.0F ||
        viewport.Height <= 0.0F)
        return {};
    const Vec2 screen{
        viewport.X + (normalized.X + 1.0F) * 0.5F * viewport.Width,
        viewport.Y + (1.0F - normalized.Y) * 0.5F * viewport.Height};
    const bool visible = normalized.Z >= 0.0F && normalized.Z <= 1.0F &&
        screen.X >= viewport.X && screen.X <= viewport.X + viewport.Width &&
        screen.Y >= viewport.Y && screen.Y <= viewport.Y + viewport.Height;
    return {screen, normalized.Z, visible};
}
}

SelectionHandles GenerateSelectionHandles(
    const SelectionBounds& bounds,
    const Vec3 modelCenter) noexcept
{
    SelectionHandles handles{};
    if (!bounds.Valid) return handles;

    // Selection bounds are inclusive voxel coordinates. Their spatial box is
    // therefore [minimum, maximum + 1], matching AppendVoxelBoxOutline.
    const Vec3 minimum{
        static_cast<float>(bounds.Minimum.X),
        static_cast<float>(bounds.Minimum.Y),
        static_cast<float>(bounds.Minimum.Z)};
    const Vec3 maximum{
        static_cast<float>(bounds.Maximum.X) + 1.0F,
        static_cast<float>(bounds.Maximum.Y) + 1.0F,
        static_cast<float>(bounds.Maximum.Z) + 1.0F};
    const Vec3 center{
        (minimum.X + maximum.X) * 0.5F,
        (minimum.Y + maximum.Y) * 0.5F,
        (minimum.Z + maximum.Z) * 0.5F};
    const auto toWorld = [modelCenter](const Vec3 position) noexcept
    {
        return position - modelCenter;
    };
    handles = {{
        {SelectionFace::XMinimum,
         toWorld({minimum.X, center.Y, center.Z})},
        {SelectionFace::XMaximum,
         toWorld({maximum.X, center.Y, center.Z})},
        {SelectionFace::YMinimum,
         toWorld({center.X, minimum.Y, center.Z})},
        {SelectionFace::YMaximum,
         toWorld({center.X, maximum.Y, center.Z})},
        {SelectionFace::ZMinimum,
         toWorld({center.X, center.Y, minimum.Z})},
        {SelectionFace::ZMaximum,
         toWorld({center.X, center.Y, maximum.Z})}
    }};
    return handles;
}

SelectionHandles ProjectSelectionHandles(
    const SelectionBounds& bounds,
    const Vec3 modelCenter,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept
{
    SelectionHandles handles = GenerateSelectionHandles(bounds, modelCenter);
    for (SelectionHandle& handle : handles)
    {
        if (handle.Face == SelectionFace::None) continue;
        const ProjectedPoint point =
            ProjectPoint(handle.WorldPosition, viewport, viewProjection);
        const ProjectedPoint axisPoint = ProjectPoint(
            handle.WorldPosition + PositiveAxis(handle.Face),
            viewport, viewProjection);
        handle.ScreenPosition = point.Screen;
        handle.Depth = point.Depth;
        handle.Visible = point.Visible;
        handle.ScreenAxisPerVoxel = {
            axisPoint.Screen.X - point.Screen.X,
            axisPoint.Screen.Y - point.Screen.Y};
        const float axisLengthSquared =
            handle.ScreenAxisPerVoxel.X * handle.ScreenAxisPerVoxel.X +
            handle.ScreenAxisPerVoxel.Y * handle.ScreenAxisPerVoxel.Y;
        if (!std::isfinite(axisLengthSquared) ||
            axisLengthSquared <
                MinimumProjectedAxisLength * MinimumProjectedAxisLength)
            handle.ScreenAxisPerVoxel = FallbackScreenAxis;
    }
    return handles;
}

std::optional<SelectionHandle> PickSelectionHandle(
    const SelectionHandles& handles,
    const Vec2 pointerScreenPosition,
    const float radiusPixels) noexcept
{
    if (!std::isfinite(pointerScreenPosition.X) ||
        !std::isfinite(pointerScreenPosition.Y) ||
        !std::isfinite(radiusPixels) || radiusPixels <= 0.0F)
        return std::nullopt;
    const float maximumDistanceSquared = radiusPixels * radiusPixels;
    float bestDistanceSquared = maximumDistanceSquared;
    float bestDepth = std::numeric_limits<float>::max();
    std::optional<SelectionHandle> best;
    for (const SelectionHandle& handle : handles)
    {
        if (!handle.Visible) continue;
        const float deltaX = pointerScreenPosition.X - handle.ScreenPosition.X;
        const float deltaY = pointerScreenPosition.Y - handle.ScreenPosition.Y;
        const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
        if (distanceSquared < bestDistanceSquared ||
            (distanceSquared == bestDistanceSquared &&
             handle.Depth < bestDepth))
        {
            bestDistanceSquared = distanceSquared;
            bestDepth = handle.Depth;
            best = handle;
        }
    }
    return best;
}

SelectionBounds ResizeSelectionBounds(
    const SelectionBounds& original,
    const SelectionFace face,
    const std::int32_t gridDelta,
    const Asset::Voxel::VoxelDimensions documentDimensions) noexcept
{
    if (!original.Valid || face == SelectionFace::None ||
        documentDimensions.X == 0U || documentDimensions.Y == 0U ||
        documentDimensions.Z == 0U)
        return original;

    SelectionBounds result = original.ClampedTo(documentDimensions);
    const auto moveMinimum = [gridDelta](
        std::int32_t& moving,
        const std::int32_t opposite,
        const std::uint32_t size)
    {
        const std::int64_t requested =
            static_cast<std::int64_t>(moving) + gridDelta;
        moving = static_cast<std::int32_t>(std::clamp<std::int64_t>(
            requested, 0, std::min<std::int64_t>(opposite, size - 1U)));
    };
    const auto moveMaximum = [gridDelta](
        std::int32_t& moving,
        const std::int32_t opposite,
        const std::uint32_t size)
    {
        const std::int64_t requested =
            static_cast<std::int64_t>(moving) + gridDelta;
        moving = static_cast<std::int32_t>(std::clamp<std::int64_t>(
            requested, std::max<std::int64_t>(0, opposite), size - 1U));
    };

    switch (face)
    {
    case SelectionFace::XMinimum:
        moveMinimum(result.Minimum.X, result.Maximum.X, documentDimensions.X);
        break;
    case SelectionFace::XMaximum:
        moveMaximum(result.Maximum.X, result.Minimum.X, documentDimensions.X);
        break;
    case SelectionFace::YMinimum:
        moveMinimum(result.Minimum.Y, result.Maximum.Y, documentDimensions.Y);
        break;
    case SelectionFace::YMaximum:
        moveMaximum(result.Maximum.Y, result.Minimum.Y, documentDimensions.Y);
        break;
    case SelectionFace::ZMinimum:
        moveMinimum(result.Minimum.Z, result.Maximum.Z, documentDimensions.Z);
        break;
    case SelectionFace::ZMaximum:
        moveMaximum(result.Maximum.Z, result.Minimum.Z, documentDimensions.Z);
        break;
    case SelectionFace::None:
    default: break;
    }
    return result;
}

} // namespace VoxelForge::Editor
