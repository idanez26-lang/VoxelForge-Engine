#include "VoxelRaycast.h"

#include "VoxelForge/Voxel/VoxelGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace VoxelForge::Editor
{
namespace
{
constexpr float DirectionEpsilon = 1.0e-7F;
constexpr float BoundaryEpsilon = 1.0e-5F;

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

bool IsOccupied(
    const Voxel::VoxelGrid& grid,
    const VoxelCoordinates coordinates,
    std::uint8_t& colorIndex) noexcept
{
    const Voxel::Voxel* voxel = grid.Get(
        coordinates.X, coordinates.Y, coordinates.Z);
    if (voxel == nullptr || !voxel->IsOccupied())
    {
        return false;
    }
    colorIndex = voxel->ColorIndex;
    return true;
}

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
}

std::optional<VoxelRaycastHit> RaycastVoxelGrid(
    const Voxel::VoxelGrid& grid,
    const VoxelRay& ray) noexcept
{
    if (grid.Width() == 0U || grid.Height() == 0U || grid.Depth() == 0U)
    {
        return std::nullopt;
    }
    if (!std::isfinite(ray.Origin.X) || !std::isfinite(ray.Origin.Y) ||
        !std::isfinite(ray.Origin.Z) ||
        !std::isfinite(ray.Direction.X) ||
        !std::isfinite(ray.Direction.Y) ||
        !std::isfinite(ray.Direction.Z))
    {
        return std::nullopt;
    }
    const Vec3 direction = Normalize(ray.Direction);
    if (Length(direction) <= DirectionEpsilon)
    {
        return std::nullopt;
    }

    GridIntersection intersection{
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), VoxelHitFace::None};
    if (!UpdateSlab(ray.Origin.X, direction.X,
            static_cast<float>(grid.Width()), VoxelHitFace::NegativeX,
            VoxelHitFace::PositiveX, intersection) ||
        !UpdateSlab(ray.Origin.Y, direction.Y,
            static_cast<float>(grid.Height()), VoxelHitFace::NegativeY,
            VoxelHitFace::PositiveY, intersection) ||
        !UpdateSlab(ray.Origin.Z, direction.Z,
            static_cast<float>(grid.Depth()), VoxelHitFace::NegativeZ,
            VoxelHitFace::PositiveZ, intersection) ||
        intersection.Exit < 0.0F)
    {
        return std::nullopt;
    }

    const bool strictlyInside =
        ray.Origin.X > 0.0F && ray.Origin.X < static_cast<float>(grid.Width()) &&
        ray.Origin.Y > 0.0F && ray.Origin.Y < static_cast<float>(grid.Height()) &&
        ray.Origin.Z > 0.0F && ray.Origin.Z < static_cast<float>(grid.Depth());
    if (!strictlyInside && intersection.Exit <= BoundaryEpsilon &&
        intersection.Entry <= 0.0F)
    {
        return std::nullopt;
    }

    const float startDistance = std::max(intersection.Entry, 0.0F);
    const Vec3 start = ray.Origin + direction * startDistance;
    VoxelCoordinates cell{
        InitialCell(start.X, direction.X, grid.Width()),
        InitialCell(start.Y, direction.Y, grid.Height()),
        InitialCell(start.Z, direction.Z, grid.Depth())};
    VoxelHitFace face = StartingFace(
        start, direction, intersection.EntryFace, strictlyInside);

    const int stepX = direction.X > DirectionEpsilon ? 1 :
        (direction.X < -DirectionEpsilon ? -1 : 0);
    const int stepY = direction.Y > DirectionEpsilon ? 1 :
        (direction.Y < -DirectionEpsilon ? -1 : 0);
    const int stepZ = direction.Z > DirectionEpsilon ? 1 :
        (direction.Z < -DirectionEpsilon ? -1 : 0);
    const float infinity = std::numeric_limits<float>::infinity();
    const auto firstBoundary = [](const std::uint32_t coordinate, const int step)
    {
        return static_cast<float>(coordinate + (step > 0 ? 1U : 0U));
    };
    float nextX = stepX == 0 ? infinity :
        (firstBoundary(cell.X, stepX) - ray.Origin.X) / direction.X;
    float nextY = stepY == 0 ? infinity :
        (firstBoundary(cell.Y, stepY) - ray.Origin.Y) / direction.Y;
    float nextZ = stepZ == 0 ? infinity :
        (firstBoundary(cell.Z, stepZ) - ray.Origin.Z) / direction.Z;
    const float deltaX = stepX == 0 ? infinity : std::abs(1.0F / direction.X);
    const float deltaY = stepY == 0 ? infinity : std::abs(1.0F / direction.Y);
    const float deltaZ = stepZ == 0 ? infinity : std::abs(1.0F / direction.Z);

    const std::uint64_t maximumSteps =
        static_cast<std::uint64_t>(grid.Width()) + grid.Height() +
        grid.Depth() + 3U;
    float distance = startDistance;
    for (std::uint64_t iteration = 0U; iteration < maximumSteps; ++iteration)
    {
        std::uint8_t colorIndex = 0U;
        if (IsOccupied(grid, cell, colorIndex))
        {
            return VoxelRaycastHit{
                cell, face, distance, ray.Origin + direction * distance,
                colorIndex};
        }

        if (nextX <= nextY && nextX <= nextZ)
        {
            distance = nextX;
            nextX += deltaX;
            const auto next = static_cast<std::int64_t>(cell.X) + stepX;
            if (next < 0 || next >= static_cast<std::int64_t>(grid.Width())) break;
            cell.X = static_cast<std::uint32_t>(next);
            face = stepX > 0 ? VoxelHitFace::NegativeX : VoxelHitFace::PositiveX;
        }
        else if (nextY <= nextZ)
        {
            distance = nextY;
            nextY += deltaY;
            const auto next = static_cast<std::int64_t>(cell.Y) + stepY;
            if (next < 0 || next >= static_cast<std::int64_t>(grid.Height())) break;
            cell.Y = static_cast<std::uint32_t>(next);
            face = stepY > 0 ? VoxelHitFace::NegativeY : VoxelHitFace::PositiveY;
        }
        else
        {
            distance = nextZ;
            nextZ += deltaZ;
            const auto next = static_cast<std::int64_t>(cell.Z) + stepZ;
            if (next < 0 || next >= static_cast<std::int64_t>(grid.Depth())) break;
            cell.Z = static_cast<std::uint32_t>(next);
            face = stepZ > 0 ? VoxelHitFace::NegativeZ : VoxelHitFace::PositiveZ;
        }
        if (distance > intersection.Exit + BoundaryEpsilon) break;
    }
    return std::nullopt;
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
