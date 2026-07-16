#include "ViewportRayBuilder.h"

#include <cmath>

namespace VoxelForge::Editor
{

ViewportRayBuildResult BuildViewportRay(
    const Vec2 mouseScreenPosition,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection,
    const Vec3 cameraWorldPosition) noexcept
{
    if (!std::isfinite(mouseScreenPosition.X) ||
        !std::isfinite(mouseScreenPosition.Y) ||
        !std::isfinite(viewport.X) || !std::isfinite(viewport.Y) ||
        !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) ||
        viewport.Width <= 0.0F || viewport.Height <= 0.0F ||
        !IsFinite(viewProjection) || !IsFinite(cameraWorldPosition))
    {
        return {ViewportRayBuildError::InvalidInput, std::nullopt};
    }
    if (mouseScreenPosition.X < viewport.X ||
        mouseScreenPosition.Y < viewport.Y ||
        mouseScreenPosition.X > viewport.X + viewport.Width ||
        mouseScreenPosition.Y > viewport.Y + viewport.Height)
    {
        return {ViewportRayBuildError::OutsideViewport, std::nullopt};
    }
    const std::optional<Matrix4> inverse = InvertMatrix(viewProjection);
    if (!inverse)
    {
        return {
            ViewportRayBuildError::NonInvertibleViewProjection,
            std::nullopt};
    }
    const float normalizedX =
        2.0F * (mouseScreenPosition.X - viewport.X) / viewport.Width - 1.0F;
    const float normalizedY =
        1.0F - 2.0F * (mouseScreenPosition.Y - viewport.Y) / viewport.Height;
    const Vec3 farWorld = TransformPoint(*inverse, {normalizedX, normalizedY, 1.0F});
    const Vec3 direction = Normalize(farWorld - cameraWorldPosition);
    if (!IsFinite(farWorld) || !IsFinite(direction) || Length(direction) <= 1.0e-7F)
    {
        return {ViewportRayBuildError::InvalidResult, std::nullopt};
    }
    return {
        ViewportRayBuildError::None,
        VoxelRay{cameraWorldPosition, direction}};
}

} // namespace VoxelForge::Editor
