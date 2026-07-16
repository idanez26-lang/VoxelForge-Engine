#include "VoxelRaycast.h"

#include "VoxelForge/Voxel/VoxelGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace VoxelForge::Editor
{
namespace
{
constexpr float DirectionEpsilon = 1.0e-7F;
constexpr float BoundaryEpsilon = VoxelRaycastEpsilon;

struct GridIntersection final
{
    float Entry = 0.0F;
    float Exit = 0.0F;
    VoxelHitFace EntryFace = VoxelHitFace::None;
};

bool UpdateSlab(
    const float origin,
    const float direction,
    const float maximum,
    const VoxelHitFace minimumFace,
    const VoxelHitFace maximumFace,
    GridIntersection& intersection) noexcept
{
    if (std::abs(direction) <= DirectionEpsilon)
    {
        return origin >= 0.0F && origin <= maximum;
    }

    float first = (0.0F - origin) / direction;
    float second = (maximum - origin) / direction;
    VoxelHitFace firstFace = minimumFace;
    if (first > second)
    {
        std::swap(first, second);
        firstFace = maximumFace;
    }
    if (first > intersection.Entry)
    {
        intersection.Entry = first;
        intersection.EntryFace = firstFace;
    }
    intersection.Exit = std::min(intersection.Exit, second);
    return intersection.Entry <= intersection.Exit + BoundaryEpsilon;
}

std::uint32_t InitialCell(
    const float position,
    const float direction,
    const std::uint32_t dimension) noexcept
{
    float adjusted = position;
    if (direction < 0.0F &&
        std::abs(position - std::round(position)) <= BoundaryEpsilon)
    {
        adjusted -= BoundaryEpsilon;
    }
    const float upper = static_cast<float>(dimension) - BoundaryEpsilon;
    return static_cast<std::uint32_t>(
        std::floor(std::clamp(adjusted, 0.0F, upper)));
}

struct TraversalHit final
{
    VoxelCoordinates Coordinates{};
    VoxelHitFace Face = VoxelHitFace::None;
    float Distance = 0.0F;
    std::uint8_t ColorIndex = 0U;
};

VoxelHitFace StartingFace(
    const Vec3 point,
    const Vec3 direction,
    const VoxelHitFace gridEntryFace,
    const bool strictlyInside) noexcept
{
    if (!strictlyInside) return gridEntryFace;
    const auto onBoundary = [](const float value)
    {
        return std::abs(value - std::round(value)) <= BoundaryEpsilon;
    };
    if (onBoundary(point.X) && point.X > 0.0F &&
        std::abs(direction.X) > DirectionEpsilon)
        return direction.X < 0.0F ? VoxelHitFace::PositiveX
                                  : VoxelHitFace::NegativeX;
    if (onBoundary(point.Y) && point.Y > 0.0F &&
        std::abs(direction.Y) > DirectionEpsilon)
        return direction.Y < 0.0F ? VoxelHitFace::PositiveY
                                  : VoxelHitFace::NegativeY;
    if (onBoundary(point.Z) && point.Z > 0.0F &&
        std::abs(direction.Z) > DirectionEpsilon)
        return direction.Z < 0.0F ? VoxelHitFace::PositiveZ
                                  : VoxelHitFace::NegativeZ;
    return VoxelHitFace::None;
}

template<typename Occupancy>
std::optional<TraversalHit> TraverseGrid(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t depth,
    const VoxelRay& ray,
    const float maximumDistance,
    const std::uint64_t requestedMaximumSteps,
    Occupancy&& occupancy) noexcept
{
    if (width == 0U || height == 0U || depth == 0U ||
        !IsFinite(ray.Origin) || !IsFinite(ray.Direction) ||
        Length(ray.Direction) <= DirectionEpsilon ||
        !std::isfinite(maximumDistance) || maximumDistance < 0.0F ||
        requestedMaximumSteps == 0U)
    {
        return std::nullopt;
    }

    GridIntersection intersection{
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), VoxelHitFace::None};
    if (!UpdateSlab(ray.Origin.X, ray.Direction.X,
            static_cast<float>(width), VoxelHitFace::NegativeX,
            VoxelHitFace::PositiveX, intersection) ||
        !UpdateSlab(ray.Origin.Y, ray.Direction.Y,
            static_cast<float>(height), VoxelHitFace::NegativeY,
            VoxelHitFace::PositiveY, intersection) ||
        !UpdateSlab(ray.Origin.Z, ray.Direction.Z,
            static_cast<float>(depth), VoxelHitFace::NegativeZ,
            VoxelHitFace::PositiveZ, intersection) ||
        intersection.Exit < 0.0F)
    {
        return std::nullopt;
    }

    const bool strictlyInside =
        ray.Origin.X > 0.0F && ray.Origin.X < static_cast<float>(width) &&
        ray.Origin.Y > 0.0F && ray.Origin.Y < static_cast<float>(height) &&
        ray.Origin.Z > 0.0F && ray.Origin.Z < static_cast<float>(depth);
    if (!strictlyInside && intersection.Exit <= BoundaryEpsilon &&
        intersection.Entry <= 0.0F)
    {
        return std::nullopt;
    }

    const float startDistance = std::max(intersection.Entry, 0.0F);
    if (startDistance > maximumDistance + BoundaryEpsilon)
        return std::nullopt;
    const Vec3 start = ray.Origin + ray.Direction * startDistance;
    VoxelCoordinates cell{
        InitialCell(start.X, ray.Direction.X, width),
        InitialCell(start.Y, ray.Direction.Y, height),
        InitialCell(start.Z, ray.Direction.Z, depth)};
    VoxelHitFace face = StartingFace(
        start, ray.Direction, intersection.EntryFace, strictlyInside);

    const int stepX = ray.Direction.X > DirectionEpsilon ? 1 :
        (ray.Direction.X < -DirectionEpsilon ? -1 : 0);
    const int stepY = ray.Direction.Y > DirectionEpsilon ? 1 :
        (ray.Direction.Y < -DirectionEpsilon ? -1 : 0);
    const int stepZ = ray.Direction.Z > DirectionEpsilon ? 1 :
        (ray.Direction.Z < -DirectionEpsilon ? -1 : 0);
    const float infinity = std::numeric_limits<float>::infinity();
    const auto firstBoundary = [](const std::uint32_t coordinate, const int step)
    {
        return static_cast<float>(coordinate + (step > 0 ? 1U : 0U));
    };
    float nextX = stepX == 0 ? infinity :
        (firstBoundary(cell.X, stepX) - ray.Origin.X) / ray.Direction.X;
    float nextY = stepY == 0 ? infinity :
        (firstBoundary(cell.Y, stepY) - ray.Origin.Y) / ray.Direction.Y;
    float nextZ = stepZ == 0 ? infinity :
        (firstBoundary(cell.Z, stepZ) - ray.Origin.Z) / ray.Direction.Z;
    const float deltaX = stepX == 0 ? infinity :
        std::abs(1.0F / ray.Direction.X);
    const float deltaY = stepY == 0 ? infinity :
        std::abs(1.0F / ray.Direction.Y);
    const float deltaZ = stepZ == 0 ? infinity :
        std::abs(1.0F / ray.Direction.Z);

    const std::uint64_t dimensionStepLimit =
        static_cast<std::uint64_t>(width) + height + depth + 3U;
    const std::uint64_t maximumSteps = std::min(
        requestedMaximumSteps,
        std::min(dimensionStepLimit, MaximumVoxelRaycastSteps));
    float distance = startDistance;
    for (std::uint64_t iteration = 0U; iteration < maximumSteps; ++iteration)
    {
        if (const std::optional<std::uint8_t> color = occupancy(cell))
            return TraversalHit{cell, face, distance, *color};

        const float nextDistance = std::min({nextX, nextY, nextZ});
        if (!std::isfinite(nextDistance) ||
            nextDistance > intersection.Exit + BoundaryEpsilon ||
            nextDistance > maximumDistance + BoundaryEpsilon)
        {
            break;
        }
        const bool crossX = std::abs(nextX - nextDistance) <= BoundaryEpsilon;
        const bool crossY = std::abs(nextY - nextDistance) <= BoundaryEpsilon;
        const bool crossZ = std::abs(nextZ - nextDistance) <= BoundaryEpsilon;
        distance = nextDistance;

        // Simultaneous crossings advance every tied axis so cells touched only
        // at an edge/corner are not reported. X, then Y, then Z determines the
        // face for deterministic diagnostics when several axes are equal.
        if (crossX)
            face = stepX > 0 ? VoxelHitFace::NegativeX : VoxelHitFace::PositiveX;
        else if (crossY)
            face = stepY > 0 ? VoxelHitFace::NegativeY : VoxelHitFace::PositiveY;
        else
            face = stepZ > 0 ? VoxelHitFace::NegativeZ : VoxelHitFace::PositiveZ;

        bool withinBounds = true;
        const auto advance = [&withinBounds](
            std::uint32_t& coordinate,
            const int step,
            const std::uint32_t dimension)
        {
            const std::int64_t next =
                static_cast<std::int64_t>(coordinate) + step;
            if (next < 0 || next >= static_cast<std::int64_t>(dimension))
            {
                withinBounds = false;
                return;
            }
            coordinate = static_cast<std::uint32_t>(next);
        };
        if (crossX) { advance(cell.X, stepX, width); nextX += deltaX; }
        if (crossY) { advance(cell.Y, stepY, height); nextY += deltaY; }
        if (crossZ) { advance(cell.Z, stepZ, depth); nextZ += deltaZ; }
        if (!withinBounds) break;
    }
    return std::nullopt;
}

bool TransformPairIsValid(const VoxelModelTransform& transform) noexcept
{
    if (!IsFinite(transform.ModelMatrix) ||
        !IsFinite(transform.InverseModelMatrix)) return false;
    const Matrix4 product = MultiplyMatrix(
        transform.ModelMatrix, transform.InverseModelMatrix);
    const Matrix4 identity = IdentityMatrix();
    for (std::size_t index = 0U; index < product.size(); ++index)
    {
        if (std::abs(product[index] - identity[index]) > 2.0e-3F)
            return false;
    }
    return true;
}
}

std::optional<VoxelRaycastHit> RaycastVoxelGrid(
    const Voxel::VoxelGrid& grid,
    const VoxelRay& ray) noexcept
{
    const Vec3 direction = Normalize(ray.Direction);
    const auto hit = TraverseGrid(
        grid.Width(), grid.Height(), grid.Depth(),
        {ray.Origin, direction}, DefaultVoxelRaycastMaximumDistance,
        MaximumVoxelRaycastSteps,
        [&grid](const VoxelCoordinates coordinates)
            -> std::optional<std::uint8_t>
        {
            const Voxel::Voxel* voxel = grid.Get(
                coordinates.X, coordinates.Y, coordinates.Z);
            return voxel != nullptr && voxel->IsOccupied()
                ? std::optional<std::uint8_t>(voxel->ColorIndex)
                : std::nullopt;
        });
    if (!hit) return std::nullopt;
    const Vec3 impact = ray.Origin + direction * hit->Distance;
    return VoxelRaycastHit{
        hit->Coordinates, hit->Face, hit->Distance, impact,
        hit->ColorIndex};
}

std::optional<VoxelRaycastHit> RaycastVoxelDocument(
    const Asset::Voxel::VoxelDocument& document,
    const VoxelRay& worldRay,
    const VoxelRaycastOptions& options) noexcept
{
    const Asset::Voxel::VoxelSubModel* model =
        document.GetModel(options.SubModelIndex);
    if (model == nullptr || !TransformPairIsValid(options.Transform) ||
        !IsFinite(worldRay.Origin) || !IsFinite(worldRay.Direction))
    {
        return std::nullopt;
    }
    const Vec3 worldDirection = Normalize(worldRay.Direction);
    if (Length(worldDirection) <= DirectionEpsilon) return std::nullopt;
    const VoxelRay localRay{
        TransformPoint(options.Transform.InverseModelMatrix, worldRay.Origin),
        TransformVector(options.Transform.InverseModelMatrix, worldDirection)};
    if (!IsFinite(localRay.Origin) || !IsFinite(localRay.Direction))
        return std::nullopt;
    const Asset::Voxel::VoxelDimensions dimensions = model->Dimensions();
    const auto hit = TraverseGrid(
        dimensions.X, dimensions.Y, dimensions.Z, localRay,
        options.MaximumDistance, options.MaximumSteps,
        [model](const VoxelCoordinates coordinates)
            -> std::optional<std::uint8_t>
        {
            const auto voxel = model->GetVoxel({
                static_cast<std::int32_t>(coordinates.X),
                static_cast<std::int32_t>(coordinates.Y),
                static_cast<std::int32_t>(coordinates.Z)});
            return voxel
                ? std::optional<std::uint8_t>(voxel->PaletteIndex)
                : std::nullopt;
        });
    if (!hit) return std::nullopt;

    const Asset::Voxel::VoxelPosition voxelPosition{
        static_cast<std::int32_t>(hit->Coordinates.X),
        static_cast<std::int32_t>(hit->Coordinates.Y),
        static_cast<std::int32_t>(hit->Coordinates.Z)};
    const Asset::Voxel::VoxelPosition integerNormal =
        VoxelHitFaceIntegerNormal(hit->Face);
    const Asset::Voxel::VoxelPosition adjacent{
        voxelPosition.X + integerNormal.X,
        voxelPosition.Y + integerNormal.Y,
        voxelPosition.Z + integerNormal.Z};
    const bool adjacentWithinBounds =
        hit->Face != VoxelHitFace::None &&
        adjacent.X >= 0 && adjacent.Y >= 0 && adjacent.Z >= 0 &&
        static_cast<std::uint32_t>(adjacent.X) < dimensions.X &&
        static_cast<std::uint32_t>(adjacent.Y) < dimensions.Y &&
        static_cast<std::uint32_t>(adjacent.Z) < dimensions.Z;
    const Vec3 localPosition =
        localRay.Origin + localRay.Direction * hit->Distance;
    const Vec3 worldPosition = TransformPoint(
        options.Transform.ModelMatrix, localPosition);
    const Vec3 localNormal = VoxelHitFaceNormal(hit->Face);
    const Matrix4& inverse = options.Transform.InverseModelMatrix;
    const Vec3 worldNormal = Normalize({
        inverse[0] * localNormal.X + inverse[4] * localNormal.Y +
            inverse[8] * localNormal.Z,
        inverse[1] * localNormal.X + inverse[5] * localNormal.Y +
            inverse[9] * localNormal.Z,
        inverse[2] * localNormal.X + inverse[6] * localNormal.Y +
            inverse[10] * localNormal.Z});
    if (!IsFinite(localPosition) || !IsFinite(worldPosition) ||
        !IsFinite(worldNormal)) return std::nullopt;
    return VoxelRaycastHit{
        hit->Coordinates,
        hit->Face,
        hit->Distance,
        localPosition,
        hit->ColorIndex,
        options.SubModelIndex,
        adjacent,
        adjacentWithinBounds,
        localPosition,
        worldPosition,
        worldNormal};
}

Asset::Voxel::VoxelPosition VoxelHitFaceIntegerNormal(
    const VoxelHitFace face) noexcept
{
    switch (face)
    {
    case VoxelHitFace::NegativeX: return {-1, 0, 0};
    case VoxelHitFace::PositiveX: return {1, 0, 0};
    case VoxelHitFace::NegativeY: return {0, -1, 0};
    case VoxelHitFace::PositiveY: return {0, 1, 0};
    case VoxelHitFace::NegativeZ: return {0, 0, -1};
    case VoxelHitFace::PositiveZ: return {0, 0, 1};
    case VoxelHitFace::None: return {};
    }
    return {};
}

Vec3 VoxelHitFaceNormal(const VoxelHitFace face) noexcept
{
    const Asset::Voxel::VoxelPosition normal =
        VoxelHitFaceIntegerNormal(face);
    return {
        static_cast<float>(normal.X),
        static_cast<float>(normal.Y),
        static_cast<float>(normal.Z)};
}

const char* VoxelHitFaceName(const VoxelHitFace face) noexcept
{
    switch (face)
    {
    case VoxelHitFace::NegativeX: return "-X";
    case VoxelHitFace::PositiveX: return "+X";
    case VoxelHitFace::NegativeY: return "-Y";
    case VoxelHitFace::PositiveY: return "+Y";
    case VoxelHitFace::NegativeZ: return "-Z";
    case VoxelHitFace::PositiveZ: return "+Z";
    case VoxelHitFace::None: return "Inside";
    }
    return "Unknown";
}

} // namespace VoxelForge::Editor
