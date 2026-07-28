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
    const UniversalCursorPreviewSubject subject,
    const bool strokeActive) noexcept
{
    return subject != UniversalCursorPreviewSubject::PencilSingleVoxel ||
        strokeActive;
}

bool ShouldRetainExactPreviewOnMissingFrame(
    const UniversalCursorPreviewSubject subject,
    const bool strokeActive) noexcept
{
    return strokeActive &&
        (subject == UniversalCursorPreviewSubject::PencilSingleVoxel ||
         subject == UniversalCursorPreviewSubject::PencilBrush);
}

bool ShouldResolvePreviewForPresentation(const bool strokeActive) noexcept
{
    return !strokeActive;
}

bool ShouldPresentFaceAddAsPlanGhosts(
    const bool faceGeometry,
    const bool addAction,
    const bool strokeActive) noexcept
{
    return faceGeometry && addAction && strokeActive;
}

std::optional<UniversalCursor2DTarget> SelectUniversalCursor2DTarget(
    std::optional<UniversalCursor2DTarget> hovered,
    std::optional<UniversalCursor2DTarget> planned,
    const UniversalCursorAnchorPolicy policy) noexcept
{
    if (policy == UniversalCursorAnchorPolicy::PreferPlannedTarget && planned)
        return planned;
    return hovered ? hovered : planned;
}

UniversalCursor2DTarget MakeVoxelFaceCursor2DTarget(
    const Asset::Voxel::VoxelPosition voxel,
    const Asset::Voxel::VoxelPosition normal,
    const Vec3 modelCenter) noexcept
{
    const Vec3 faceNormal{
        static_cast<float>(normal.X),
        static_cast<float>(normal.Y),
        static_cast<float>(normal.Z)};
    const Vec3 voxelCenter{
        static_cast<float>(voxel.X) + 0.5F - modelCenter.X,
        static_cast<float>(voxel.Y) + 0.5F - modelCenter.Y,
        static_cast<float>(voxel.Z) + 0.5F - modelCenter.Z};
    return {voxelCenter + faceNormal * 0.5F, faceNormal};
}

std::optional<UniversalCursor2DTarget>
MakeOutermostVoxelFaceCursor2DTarget(
    const std::span<const Asset::Voxel::VoxelPosition> presentedPositions,
    const Asset::Voxel::VoxelPosition lockedSeed,
    const Asset::Voxel::VoxelPosition normal,
    const Vec3 modelCenter) noexcept
{
    if (presentedPositions.empty()) return std::nullopt;
    const auto coordinateAlongNormal = [normal](
        const Asset::Voxel::VoxelPosition position) noexcept
    {
        return position.X * normal.X + position.Y * normal.Y +
            position.Z * normal.Z;
    };
    std::int32_t outermostCoordinate =
        coordinateAlongNormal(presentedPositions.front());
    for (const Asset::Voxel::VoxelPosition& position : presentedPositions)
    {
        outermostCoordinate =
            std::max(outermostCoordinate, coordinateAlongNormal(position));
    }
    Asset::Voxel::VoxelPosition cursorVoxel = lockedSeed;
    if (normal.X != 0)
        cursorVoxel.X = outermostCoordinate * normal.X;
    else if (normal.Y != 0)
        cursorVoxel.Y = outermostCoordinate * normal.Y;
    else if (normal.Z != 0)
        cursorVoxel.Z = outermostCoordinate * normal.Z;
    else
        return std::nullopt;
    return MakeVoxelFaceCursor2DTarget(cursorVoxel, normal, modelCenter);
}

} // namespace VoxelForge::Editor
