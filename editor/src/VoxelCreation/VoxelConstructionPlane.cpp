#include "VoxelConstructionPlane.h"

#include "VoxelSelection/VoxelRayTransform.h"

#include <cmath>

namespace VoxelForge::Editor
{

std::optional<Asset::Voxel::VoxelPosition>
FindVoxelConstructionPlaneTarget(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    const VoxelRay& worldRay,
    const Vec3 modelCenter) noexcept
{
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions || document.GetVoxelCount() != 0U) return std::nullopt;
    const VoxelModelTransform transform =
        CenteredVoxelModelTransform(modelCenter);
    const Vec3 origin =
        TransformPoint(transform.InverseModelMatrix, worldRay.Origin);
    const Vec3 direction =
        TransformVector(transform.InverseModelMatrix, worldRay.Direction);
    if (!IsFinite(origin) || !IsFinite(direction) ||
        std::abs(direction.Y) <= 1.0e-6F)
        return std::nullopt;
    const float distance = -origin.Y / direction.Y;
    if (!std::isfinite(distance) || distance < 0.0F) return std::nullopt;
    const Vec3 point = origin + direction * distance;
    if (!IsFinite(point)) return std::nullopt;
    const Asset::Voxel::VoxelPosition target{
        static_cast<std::int32_t>(std::floor(point.X)),
        0,
        static_cast<std::int32_t>(std::floor(point.Z))};
    if (target.X < 0 || target.Z < 0 ||
        static_cast<std::uint32_t>(target.X) >= dimensions->X ||
        static_cast<std::uint32_t>(target.Z) >= dimensions->Z)
        return std::nullopt;
    return target;
}

} // namespace VoxelForge::Editor
