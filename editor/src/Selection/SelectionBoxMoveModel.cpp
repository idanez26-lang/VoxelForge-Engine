#include "SelectionBoxMoveModel.h"

#include "EditorMatrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace VoxelForge::Editor
{
namespace
{
constexpr float RayEpsilon = 1.0e-6F;

[[nodiscard]] bool IntersectSlab(
    const float origin,
    const float direction,
    const float minimum,
    const float maximum,
    float& entry,
    float& exit) noexcept
{
    if (std::abs(direction) <= RayEpsilon)
        return origin >= minimum && origin <= maximum;
    float first = (minimum - origin) / direction;
    float second = (maximum - origin) / direction;
    if (first > second) std::swap(first, second);
    entry = std::max(entry, first);
    exit = std::min(exit, second);
    return exit >= entry;
}

[[nodiscard]] std::int32_t ClampDelta(
    const std::int32_t requested,
    const std::int32_t minimum,
    const std::int32_t maximum) noexcept
{
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        requested, minimum, maximum));
}
}

std::optional<SelectionBoxRayHit> PickSelectionBoxInterior(
    const SelectionBounds& bounds,
    const Vec3 modelCenter,
    const VoxelRay& ray,
    const std::optional<float> occluderDistance) noexcept
{
    if (!bounds.Valid || !IsFinite(modelCenter) || !IsFinite(ray.Origin) ||
        !IsFinite(ray.Direction) || Length(ray.Direction) <= RayEpsilon)
        return std::nullopt;

    const Vec3 minimum{
        static_cast<float>(bounds.Minimum.X) - modelCenter.X,
        static_cast<float>(bounds.Minimum.Y) - modelCenter.Y,
        static_cast<float>(bounds.Minimum.Z) - modelCenter.Z};
    const Vec3 maximum{
        static_cast<float>(bounds.Maximum.X) + 1.0F - modelCenter.X,
        static_cast<float>(bounds.Maximum.Y) + 1.0F - modelCenter.Y,
        static_cast<float>(bounds.Maximum.Z) + 1.0F - modelCenter.Z};
    float entry = -std::numeric_limits<float>::infinity();
    float exit = std::numeric_limits<float>::infinity();
    if (!IntersectSlab(ray.Origin.X, ray.Direction.X,
            minimum.X, maximum.X, entry, exit) ||
        !IntersectSlab(ray.Origin.Y, ray.Direction.Y,
            minimum.Y, maximum.Y, entry, exit) ||
        !IntersectSlab(ray.Origin.Z, ray.Direction.Z,
            minimum.Z, maximum.Z, entry, exit) || exit < 0.0F)
        return std::nullopt;

    entry = std::max(entry, 0.0F);
    if (occluderDistance && std::isfinite(*occluderDistance) &&
        *occluderDistance >= 0.0F &&
        *occluderDistance + RayEpsilon < entry)
        return std::nullopt;
    return SelectionBoxRayHit{
        ray.Origin + ray.Direction * entry, entry, exit};
}

SelectionMovePlane MakeSelectionMovePlane(
    const Vec3 anchorWorldPosition,
    const Vec3 viewDirection) noexcept
{
    const Vec3 normal = Normalize(viewDirection);
    if (!IsFinite(anchorWorldPosition) || !IsFinite(normal) ||
        Length(normal) <= RayEpsilon)
        return {};
    return {anchorWorldPosition, normal, true};
}

std::optional<Vec3> IntersectSelectionMovePlane(
    const VoxelRay& ray,
    const SelectionMovePlane& plane) noexcept
{
    if (!plane.Valid || !IsFinite(plane.Point) || !IsFinite(plane.Normal) ||
        !IsFinite(ray.Origin) || !IsFinite(ray.Direction))
        return std::nullopt;
    const float denominator = Dot(plane.Normal, ray.Direction);
    if (!std::isfinite(denominator) || std::abs(denominator) <= RayEpsilon)
        return std::nullopt;
    const float distance =
        Dot(plane.Normal, plane.Point - ray.Origin) / denominator;
    if (!std::isfinite(distance) || distance < 0.0F) return std::nullopt;
    const Vec3 point = ray.Origin + ray.Direction * distance;
    return IsFinite(point) ? std::optional<Vec3>(point) : std::nullopt;
}

SelectionBounds TranslateSelectionBounds(
    const SelectionBounds& original,
    const Asset::Voxel::VoxelPosition requestedDelta,
    const Asset::Voxel::VoxelDimensions documentDimensions) noexcept
{
    if (!original.Valid || documentDimensions.X == 0U ||
        documentDimensions.Y == 0U || documentDimensions.Z == 0U ||
        original.Minimum.X < 0 || original.Minimum.Y < 0 ||
        original.Minimum.Z < 0 ||
        original.Maximum.X >= static_cast<std::int32_t>(documentDimensions.X) ||
        original.Maximum.Y >= static_cast<std::int32_t>(documentDimensions.Y) ||
        original.Maximum.Z >= static_cast<std::int32_t>(documentDimensions.Z))
        return original;

    const Asset::Voxel::VoxelPosition delta{
        ClampDelta(requestedDelta.X, -original.Minimum.X,
            static_cast<std::int32_t>(documentDimensions.X) - 1 -
                original.Maximum.X),
        ClampDelta(requestedDelta.Y, -original.Minimum.Y,
            static_cast<std::int32_t>(documentDimensions.Y) - 1 -
                original.Maximum.Y),
        ClampDelta(requestedDelta.Z, -original.Minimum.Z,
            static_cast<std::int32_t>(documentDimensions.Z) - 1 -
                original.Maximum.Z)};
    return SelectionBounds::FromCorners(
        {original.Minimum.X + delta.X,
         original.Minimum.Y + delta.Y,
         original.Minimum.Z + delta.Z},
        {original.Maximum.X + delta.X,
         original.Maximum.Y + delta.Y,
         original.Maximum.Z + delta.Z});
}

SelectionPointerTarget ResolveSelectionPointerTarget(
    const bool handleHovered,
    const bool interiorHovered) noexcept
{
    if (handleHovered) return SelectionPointerTarget::Handle;
    if (interiorHovered) return SelectionPointerTarget::Interior;
    return SelectionPointerTarget::Exterior;
}

} // namespace VoxelForge::Editor
