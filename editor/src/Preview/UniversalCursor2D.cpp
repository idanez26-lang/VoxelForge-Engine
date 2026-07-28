#include "UniversalCursor2D.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace VoxelForge::Editor
{
namespace
{
constexpr float MinimumNormalLength = 1.0e-4F;

[[nodiscard]] std::optional<Vec2> ProjectPoint(
    const Vec3 point,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept
{
    if (viewport.Width <= 0.0F || viewport.Height <= 0.0F ||
        !IsFinite(point) || !IsFinite(viewProjection))
        return std::nullopt;
    const Vec3 normalized = TransformPoint(viewProjection, point);
    if (!IsFinite(normalized)) return std::nullopt;
    const Vec2 screen{
        viewport.X + (normalized.X * 0.5F + 0.5F) * viewport.Width,
        viewport.Y + (0.5F - normalized.Y * 0.5F) * viewport.Height};
    if (!std::isfinite(screen.X) || !std::isfinite(screen.Y))
        return std::nullopt;
    return screen;
}

[[nodiscard]] std::array<Vec3, 2U> FaceTangents(
    const Vec3 normal) noexcept
{
    const float x = std::abs(normal.X);
    const float y = std::abs(normal.Y);
    const float z = std::abs(normal.Z);
    if (x >= y && x >= z)
        return {{{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    if (y >= x && y >= z)
        return {{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    return {{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}};
}
}

UniversalCursor2DGeometry ProjectUniversalCursor2D(
    const UniversalCursor2DTarget& target,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept
{
    UniversalCursor2DGeometry result;
    if (!IsFinite(target.FaceNormal) ||
        Length(target.FaceNormal) <= MinimumNormalLength)
        return result;

    const std::array<Vec3, 2U> tangents = FaceTangents(target.FaceNormal);
    const Vec3 halfU = tangents[0] * 0.5F;
    const Vec3 halfV = tangents[1] * 0.5F;
    const std::array<Vec3, 4U> corners{{
        target.SurfaceWorldPosition - halfU - halfV,
        target.SurfaceWorldPosition + halfU - halfV,
        target.SurfaceWorldPosition + halfU + halfV,
        target.SurfaceWorldPosition - halfU + halfV}};
    for (std::size_t index = 0U; index < corners.size(); ++index)
    {
        const std::optional<Vec2> projected = ProjectPoint(
            corners[index], viewport, viewProjection);
        if (!projected) return {};
        result.Corners[index] = *projected;
    }
    result.Visible = true;
    return result;
}

bool ShouldRenderExactPreviewGeometry(
    const UniversalCursorPreviewSubject subject) noexcept
{
    return subject != UniversalCursorPreviewSubject::PencilSingleVoxel;
}

} // namespace VoxelForge::Editor
