#pragma once

#include "EditorMatrix.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
// Input-only adapter for Face extrusion. It projects the locked outward
// normal once on MouseDown; subsequent depth values use that frozen axis, so
// orbiting or a camera update cannot change a gesture already in progress.
struct SmartToolFaceDepthDragAxis final
{
    Vec2 ScreenDirection{0.0F, -1.0F};
    bool UsesFallback = true;
};

[[nodiscard]] inline SmartToolFaceDepthDragAxis MakeSmartToolFaceDepthDragAxis(
    const Vec3 surfaceWorldPosition, const Vec3 outwardNormal,
    const ViewportRectangle& viewport, const Matrix4& viewProjection) noexcept
{
    constexpr float minimumProjectedLengthPixels = 4.0F;
    SmartToolFaceDepthDragAxis result;
    if (viewport.Width <= 0.0F || viewport.Height <= 0.0F ||
        !IsFinite(viewProjection) || !IsFinite(surfaceWorldPosition) ||
        !IsFinite(outwardNormal)) return result;

    const Vec3 start = TransformPoint(viewProjection, surfaceWorldPosition);
    const Vec3 end = TransformPoint(viewProjection, surfaceWorldPosition + outwardNormal);
    if (!IsFinite(start) || !IsFinite(end)) return result;
    const float deltaX = (end.X - start.X) * 0.5F * viewport.Width;
    // Projection Y is upward; screen Y is downward.
    const float deltaY = -(end.Y - start.Y) * 0.5F * viewport.Height;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (!std::isfinite(length) || length < minimumProjectedLengthPixels)
        return result;
    result.ScreenDirection = {deltaX / length, deltaY / length};
    result.UsesFallback = false;
    return result;
}

[[nodiscard]] inline int ResolveSmartToolFaceDepthLayers(
    const SmartToolFaceDepthDragAxis& axis, const Vec2 mouseDragPixels,
    const int minimumLayers = 1, const int maximumLayers = 64,
    const float pixelsPerLayer = 24.0F) noexcept
{
    if (minimumLayers < 1 || maximumLayers < minimumLayers ||
        !std::isfinite(mouseDragPixels.X) || !std::isfinite(mouseDragPixels.Y) ||
        !std::isfinite(axis.ScreenDirection.X) ||
        !std::isfinite(axis.ScreenDirection.Y) ||
        !std::isfinite(pixelsPerLayer) || pixelsPerLayer <= 0.0F)
        return std::max(1, minimumLayers);
    const float projectedPixels = mouseDragPixels.X * axis.ScreenDirection.X +
        mouseDragPixels.Y * axis.ScreenDirection.Y;
    const int layers = minimumLayers + static_cast<int>(std::floor(
        projectedPixels / pixelsPerLayer));
    return std::clamp(layers, minimumLayers, maximumLayers);
}

// Geometry Cylinder reuses the frozen projected normal, but unlike Face its
// height is signed and never zero. The base disk is layer zero; additional
// layers follow the sign of this value.
[[nodiscard]] inline int ResolveSmartToolGeometryHeight(
    const SmartToolFaceDepthDragAxis& axis, const Vec2 mouseDragPixels,
    const int maximumLayers = 64,
    const float pixelsPerLayer = 24.0F) noexcept
{
    if (maximumLayers < 1 ||
        !std::isfinite(mouseDragPixels.X) || !std::isfinite(mouseDragPixels.Y) ||
        !std::isfinite(axis.ScreenDirection.X) ||
        !std::isfinite(axis.ScreenDirection.Y) ||
        !std::isfinite(pixelsPerLayer) || pixelsPerLayer <= 0.0F)
        return 1;
    const float projectedPixels = mouseDragPixels.X * axis.ScreenDirection.X +
        mouseDragPixels.Y * axis.ScreenDirection.Y;
    const int magnitude = 1 + static_cast<int>(std::floor(
        std::abs(projectedPixels) / pixelsPerLayer));
    return (projectedPixels < 0.0F ? -1 : 1) *
        std::clamp(magnitude, 1, maximumLayers);
}
} // namespace VoxelForge::Editor
