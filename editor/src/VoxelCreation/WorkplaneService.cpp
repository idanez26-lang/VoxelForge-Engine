#include "VoxelCreation/WorkplaneService.h"

#include "VoxelSelection/VoxelRayTransform.h"

#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
float AxisValue(const Vec3 value, const WorkplaneAxis axis) noexcept
{
    switch (axis)
    {
    case WorkplaneAxis::X: return value.X;
    case WorkplaneAxis::Y: return value.Y;
    case WorkplaneAxis::Z: return value.Z;
    }
    return value.Y;
}

Asset::Voxel::VoxelPosition PositionOnPlane(
    const Vec3 point,
    const WorkplaneDefinition definition) noexcept
{
    Asset::Voxel::VoxelPosition result{
        static_cast<std::int32_t>(std::floor(point.X)),
        static_cast<std::int32_t>(std::floor(point.Y)),
        static_cast<std::int32_t>(std::floor(point.Z))};
    switch (definition.Axis)
    {
    case WorkplaneAxis::X: result.X = definition.Coordinate; break;
    case WorkplaneAxis::Y: result.Y = definition.Coordinate; break;
    case WorkplaneAxis::Z: result.Z = definition.Coordinate; break;
    }
    return result;
}
}

bool WorkplaneHit::IsValid() const noexcept
{
    return Status == WorkplaneHitStatus::Valid && Position.has_value();
}

bool WorkplaneHit::IsVisible() const noexcept
{
    return Position.has_value() &&
        (Status == WorkplaneHitStatus::Valid ||
         Status == WorkplaneHitStatus::OutOfBounds ||
         Status == WorkplaneHitStatus::Occupied);
}

WorkplaneService::WorkplaneService(
    const WorkplaneDefinition definition) noexcept
    : definition_(definition)
{
}

void WorkplaneService::SetDefinition(
    const WorkplaneDefinition definition) noexcept
{
    definition_ = definition;
}

const WorkplaneDefinition& WorkplaneService::Definition() const noexcept
{
    return definition_;
}

std::optional<WorkplaneGrid> WorkplaneService::Grid(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex) const noexcept
{
    if (!IsSupportedV1()) return std::nullopt;
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions) return std::nullopt;
    switch (definition_.Axis)
    {
    case WorkplaneAxis::X:
        return WorkplaneGrid{definition_, dimensions->Y, dimensions->Z};
    case WorkplaneAxis::Y:
        return WorkplaneGrid{definition_, dimensions->X, dimensions->Z};
    case WorkplaneAxis::Z:
        return WorkplaneGrid{definition_, dimensions->X, dimensions->Y};
    }
    return std::nullopt;
}

WorkplaneHit WorkplaneService::Intersect(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    const VoxelRay& worldRay,
    const Vec3 modelCenter) const noexcept
{
    if (!IsSupportedV1())
        return {WorkplaneHitStatus::UnsupportedDefinition, std::nullopt, 0.0F};
    if (!document.GetDimensions(subModelIndex)) return {};
    const VoxelModelTransform transform =
        CenteredVoxelModelTransform(modelCenter);
    const Vec3 origin =
        TransformPoint(transform.InverseModelMatrix, worldRay.Origin);
    const Vec3 direction =
        TransformVector(transform.InverseModelMatrix, worldRay.Direction);
    if (!IsFinite(origin) || !IsFinite(direction)) return {};
    const float denominator = AxisValue(direction, definition_.Axis);
    if (std::abs(denominator) <= 1.0e-6F)
        return {WorkplaneHitStatus::Parallel, std::nullopt, 0.0F};
    const float distance =
        (static_cast<float>(definition_.Coordinate) -
         AxisValue(origin, definition_.Axis)) / denominator;
    if (!std::isfinite(distance) || distance < 0.0F)
        return {WorkplaneHitStatus::BehindRay, std::nullopt, distance};
    const Vec3 point = origin + direction * distance;
    if (!IsFinite(point)) return {};
    const Asset::Voxel::VoxelPosition position =
        PositionOnPlane(point, definition_);
    return {
        ValidateTarget(document, subModelIndex, position),
        position,
        distance};
}

WorkplaneHitStatus WorkplaneService::ValidateTarget(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    const Asset::Voxel::VoxelPosition position) const noexcept
{
    if (!IsSupportedV1()) return WorkplaneHitStatus::UnsupportedDefinition;
    const auto dimensions = document.GetDimensions(subModelIndex);
    if (!dimensions) return WorkplaneHitStatus::InvalidDocument;
    const bool onPlane = definition_.Axis == WorkplaneAxis::X
        ? position.X == definition_.Coordinate
        : definition_.Axis == WorkplaneAxis::Y
        ? position.Y == definition_.Coordinate
        : position.Z == definition_.Coordinate;
    const bool inside = position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions->X &&
        static_cast<std::uint32_t>(position.Y) < dimensions->Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions->Z;
    if (!onPlane || !inside) return WorkplaneHitStatus::OutOfBounds;
    if (document.HasVoxel(position, subModelIndex))
        return WorkplaneHitStatus::Occupied;
    return WorkplaneHitStatus::Valid;
}

bool WorkplaneService::IsSupportedV1() const noexcept
{
    return definition_.Axis == WorkplaneAxis::Y &&
        definition_.Coordinate == 0;
}

} // namespace VoxelForge::Editor
