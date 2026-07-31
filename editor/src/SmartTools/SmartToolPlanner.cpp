#include "SmartTools/SmartToolPlanner.h"

#include "SmartTools/SmartToolPlan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
struct PositionHash final
{
    [[nodiscard]] std::size_t operator()(
        const Asset::Voxel::VoxelPosition position) const noexcept
    {
        const std::size_t x = static_cast<std::uint32_t>(position.X);
        const std::size_t y = static_cast<std::uint32_t>(position.Y);
        const std::size_t z = static_cast<std::uint32_t>(position.Z);
        return x ^ (y * 0x9E3779B1U) ^ (z * 0x85EBCA77U);
    }
};

using VoxelSnapshot = std::unordered_map<
    Asset::Voxel::VoxelPosition, SmartToolVoxelState, PositionHash>;
using Position = Asset::Voxel::VoxelPosition;

[[nodiscard]] bool IsInside(const Position position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions.X &&
        static_cast<std::uint32_t>(position.Y) < dimensions.Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions.Z;
}

[[nodiscard]] bool IsUnitAxisNormal(const Position normal) noexcept
{
    const auto absolute = [](const std::int32_t value) noexcept
    {
        return value < 0 ? -static_cast<std::int64_t>(value)
                         : static_cast<std::int64_t>(value);
    };
    return absolute(normal.X) + absolute(normal.Y) + absolute(normal.Z) == 1;
}

[[nodiscard]] SmartBrushOrientation ResolveCompactOrientation(
    const SmartBrushOrientation orientation, const Position normal) noexcept
{
    if (orientation != SmartBrushOrientation::Auto) return orientation;
    const std::int64_t absoluteX = std::llabs(static_cast<std::int64_t>(normal.X));
    const std::int64_t absoluteY = std::llabs(static_cast<std::int64_t>(normal.Y));
    const std::int64_t absoluteZ = std::llabs(static_cast<std::int64_t>(normal.Z));
    if (absoluteX == 0 && absoluteY == 0 && absoluteZ == 0)
        return SmartBrushOrientation::Y;
    if (absoluteX >= absoluteY && absoluteX >= absoluteZ)
        return SmartBrushOrientation::X;
    if (absoluteZ >= absoluteY) return SmartBrushOrientation::Z;
    return SmartBrushOrientation::Y;
}

[[nodiscard]] std::optional<Position> CompactVolumeAnchor(
    const SmartBrushPlacement placement, const int size) noexcept
{
    const std::int64_t depthOffset = (size - 1) / 2;
    const std::int64_t x = static_cast<std::int64_t>(placement.Target.X) +
        static_cast<std::int64_t>(placement.Normal.X) * depthOffset;
    const std::int64_t y = static_cast<std::int64_t>(placement.Target.Y) +
        static_cast<std::int64_t>(placement.Normal.Y) * depthOffset;
    const std::int64_t z = static_cast<std::int64_t>(placement.Target.Z) +
        static_cast<std::int64_t>(placement.Normal.Z) * depthOffset;
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max() ||
        z < std::numeric_limits<std::int32_t>::min() ||
        z > std::numeric_limits<std::int32_t>::max())
        return std::nullopt;
    return Position{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z)};
}

[[nodiscard]] std::optional<SmartBrushBounds> TranslateCompactBounds(
    const SmartBrushCompactLocalBounds& localBounds, const Position anchor) noexcept
{
    const auto translate = [anchor](const Position local)
        -> std::optional<Position>
    {
        const std::int64_t x = static_cast<std::int64_t>(anchor.X) + local.X;
        const std::int64_t y = static_cast<std::int64_t>(anchor.Y) + local.Y;
        const std::int64_t z = static_cast<std::int64_t>(anchor.Z) + local.Z;
        if (x < std::numeric_limits<std::int32_t>::min() ||
            x > std::numeric_limits<std::int32_t>::max() ||
            y < std::numeric_limits<std::int32_t>::min() ||
            y > std::numeric_limits<std::int32_t>::max() ||
            z < std::numeric_limits<std::int32_t>::min() ||
            z > std::numeric_limits<std::int32_t>::max())
            return std::nullopt;
        return Position{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
            static_cast<std::int32_t>(z)};
    };
    const std::optional<Position> minimum = translate(localBounds.Minimum);
    const std::optional<Position> maximum = translate(localBounds.Maximum);
    if (!minimum || !maximum) return std::nullopt;
    return SmartBrushBounds{*minimum, *maximum};
}

[[nodiscard]] bool IsCompactBoundsInside(const SmartBrushBounds& bounds,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return bounds.Minimum.X >= 0 && bounds.Minimum.Y >= 0 && bounds.Minimum.Z >= 0 &&
        static_cast<std::uint64_t>(bounds.Maximum.X) < dimensions.X &&
        static_cast<std::uint64_t>(bounds.Maximum.Y) < dimensions.Y &&
        static_cast<std::uint64_t>(bounds.Maximum.Z) < dimensions.Z;
}

[[nodiscard]] std::optional<Position> Offset(const Position position,
    const Position delta) noexcept
{
    const std::int64_t x = static_cast<std::int64_t>(position.X) + delta.X;
    const std::int64_t y = static_cast<std::int64_t>(position.Y) + delta.Y;
    const std::int64_t z = static_cast<std::int64_t>(position.Z) + delta.Z;
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max() ||
        z < std::numeric_limits<std::int32_t>::min() ||
        z > std::numeric_limits<std::int32_t>::max())
        return std::nullopt;
    return Position{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z)};
}

[[nodiscard]] std::array<Position, 4U> PlanarOffsets(
    const Position normal) noexcept
{
    if (normal.X != 0)
        return {{{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    if (normal.Y != 0)
        return {{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    return {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
}

[[nodiscard]] SmartToolVoxelState ReadSnapshot(const SmartToolRequest& request,
    VoxelSnapshot& snapshot, const Position position)
{
    const auto found = snapshot.find(position);
    if (found != snapshot.end()) return found->second;
    SmartToolVoxelState state = request.ReadVoxel(position);
    if (!state.Exists) state.PaletteIndex = 0U;
    snapshot.emplace(position, state);
    return state;
}

[[nodiscard]] SmartBrushBounds CalculateBounds(
    const std::vector<Position>& positions)
{
    SmartBrushBounds bounds{positions.front(), positions.front()};
    for (const Position position : positions)
    {
        bounds.Minimum.X = std::min(bounds.Minimum.X, position.X);
        bounds.Minimum.Y = std::min(bounds.Minimum.Y, position.Y);
        bounds.Minimum.Z = std::min(bounds.Minimum.Z, position.Z);
        bounds.Maximum.X = std::max(bounds.Maximum.X, position.X);
        bounds.Maximum.Y = std::max(bounds.Maximum.Y, position.Y);
        bounds.Maximum.Z = std::max(bounds.Maximum.Z, position.Z);
    }
    return bounds;
}

[[nodiscard]] SmartBrushResult ResolveFace(const SmartToolRequest& request,
    VoxelSnapshot& snapshot)
{
    SmartBrushResult result;
    if (!request.FaceSeed || !IsUnitAxisNormal(request.FaceSeed->Normal) ||
        request.FaceSeed->Normal != request.BrushRequest.Placement.Normal)
    {
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "Face requires an exposed voxel hit and one axis normal.";
        return result;
    }

    const Position seed = request.FaceSeed->Position;
    const Position normal = request.FaceSeed->Normal;
    const auto readSupport = [&request, &snapshot](const Position position)
    {
        if (request.ReadFaceSupportVoxel)
        {
            SmartToolVoxelState state = request.ReadFaceSupportVoxel(position);
            if (!state.Exists) state.PaletteIndex = 0U;
            return state;
        }
        return ReadSnapshot(request, snapshot, position);
    };
    const std::optional<Position> seedOutward = Offset(seed, normal);
    if (!IsInside(seed, request.BrushRequest.Dimensions) ||
        !readSupport(seed).Exists ||
        (seedOutward && IsInside(*seedOutward, request.BrushRequest.Dimensions) &&
         readSupport(*seedOutward).Exists))
    {
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "Face requires an existing exposed voxel hit.";
        return result;
    }
    if (request.Action == SmartAction::Add &&
        (request.FaceDepth < 1 || request.FaceDepth > MaximumSmartToolBrushSize))
    {
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "Face Add depth must be between 1 and 64 layers.";
        return result;
    }

    // Face resolves the complete exposed, coplanar component. It deliberately
    // ignores Brush Size and Shape: a Face is a topological surface query,
    // not a planar brush. Four planar neighbors prevent traversal into another
    // plane, face orientation, or diagonal-only component.
    std::unordered_set<Position, PositionHash> visited;
    std::deque<Position> pending;
    std::vector<Position> support;
    visited.insert(seed);
    pending.push_back(seed);
    const std::array<Position, 4U> offsets = PlanarOffsets(normal);
    while (!pending.empty())
    {
        const Position current = pending.front();
        pending.pop_front();
        support.push_back(current);
        for (const Position offset : offsets)
        {
            const std::optional<Position> neighbor = Offset(current, offset);
            if (!neighbor || !IsInside(*neighbor, request.BrushRequest.Dimensions) ||
                !visited.insert(*neighbor).second)
                continue;
            const std::optional<Position> neighborOutward = Offset(*neighbor, normal);
            const bool neighborExposed = !neighborOutward ||
                !IsInside(*neighborOutward, request.BrushRequest.Dimensions) ||
                !readSupport(*neighborOutward).Exists;
            if (readSupport(*neighbor).Exists && neighborExposed)
                pending.push_back(*neighbor);
        }
    }
    std::sort(support.begin(), support.end(), [](const Position left,
        const Position right)
    {
        if (left.X != right.X) return left.X < right.X;
        if (left.Y != right.Y) return left.Y < right.Y;
        return left.Z < right.Z;
    });

    result.Positions.reserve(support.size());
    result.ClippedPositions.reserve(support.size());
    const int depth = request.Action == SmartAction::Add ? request.FaceDepth : 1;
    for (const Position source : support)
    {
        Position finalPosition = source;
        for (int layer = 0; layer < depth; ++layer)
        {
            if (request.Action == SmartAction::Add)
            {
                const std::optional<Position> outward = Offset(finalPosition, normal);
                if (!outward || !IsInside(*outward, request.BrushRequest.Dimensions))
                {
                    result.ClippedPositions.push_back(outward.value_or(finalPosition));
                    break;
                }
                finalPosition = *outward;
            }
            result.Positions.push_back(finalPosition);
            const SmartToolVoxelState state = ReadSnapshot(request, snapshot, finalPosition);
            if (state.Exists) result.ExistingPositions.push_back(finalPosition);
            else result.AddablePositions.push_back(finalPosition);
        }
    }
    result.Statistics.Total = result.Positions.size() + result.ClippedPositions.size();
    result.Statistics.New = result.AddablePositions.size();
    result.Statistics.Existing = result.ExistingPositions.size();
    result.Statistics.Clipped = result.ClippedPositions.size();
    if (result.Positions.empty() && result.ClippedPositions.empty())
    {
        // A valid plane may contain only holes or occluded support cells. It
        // is a no-change plan, not an error and not an out-of-bounds request.
        result.Code = SmartBrushResultCode::Valid;
        return result;
    }
    if (result.Positions.empty())
    {
        result.Code = SmartBrushResultCode::OutOfBounds;
        return result;
    }
    result.RenderPlan.Mode = SmartBrushRenderMode::DetailedCells;
    result.RenderPlan.Bounds = CalculateBounds(result.Positions);
    result.Code = SmartBrushResultCode::Valid;
    return result;
}

[[nodiscard]] std::vector<Position> RasterizeLine(const Position pointA,
    const Position pointB)
{
    // Bresenham tie decisions depend on its traversal direction. Canonicalize
    // the endpoints before rasterizing so A->B and B->A select the same voxel
    // set; the request itself still retains B as the placement target.
    const auto before = [](const Position left, const Position right) noexcept
    {
        if (left.X != right.X) return left.X < right.X;
        if (left.Y != right.Y) return left.Y < right.Y;
        return left.Z < right.Z;
    };
    Position start = pointA;
    Position end = pointB;
    if (before(end, start)) std::swap(start, end);
    const std::int64_t dx = std::llabs(static_cast<std::int64_t>(end.X) - start.X);
    const std::int64_t dy = std::llabs(static_cast<std::int64_t>(end.Y) - start.Y);
    const std::int64_t dz = std::llabs(static_cast<std::int64_t>(end.Z) - start.Z);
    const std::int64_t count = std::max({dx, dy, dz}) + 1;
    if (count > 1'000'000LL)
        throw std::length_error("Smart Tool Line exceeds the safe sample limit.");
    const std::int32_t stepX = end.X >= start.X ? 1 : -1;
    const std::int32_t stepY = end.Y >= start.Y ? 1 : -1;
    const std::int32_t stepZ = end.Z >= start.Z ? 1 : -1;
    std::vector<Position> points;
    points.reserve(static_cast<std::size_t>(count));
    Position current = start;
    points.push_back(current);
    if (dx >= dy && dx >= dz)
    {
        std::int64_t errorY = 2 * dy - dx;
        std::int64_t errorZ = 2 * dz - dx;
        while (current.X != end.X)
        {
            current.X += stepX;
            if (errorY >= 0) { current.Y += stepY; errorY -= 2 * dx; }
            if (errorZ >= 0) { current.Z += stepZ; errorZ -= 2 * dx; }
            errorY += 2 * dy;
            errorZ += 2 * dz;
            points.push_back(current);
        }
    }
    else if (dy >= dx && dy >= dz)
    {
        std::int64_t errorX = 2 * dx - dy;
        std::int64_t errorZ = 2 * dz - dy;
        while (current.Y != end.Y)
        {
            current.Y += stepY;
            if (errorX >= 0) { current.X += stepX; errorX -= 2 * dy; }
            if (errorZ >= 0) { current.Z += stepZ; errorZ -= 2 * dy; }
            errorX += 2 * dx;
            errorZ += 2 * dz;
            points.push_back(current);
        }
    }
    else
    {
        std::int64_t errorX = 2 * dx - dz;
        std::int64_t errorY = 2 * dy - dz;
        while (current.Z != end.Z)
        {
            current.Z += stepZ;
            if (errorX >= 0) { current.X += stepX; errorX -= 2 * dz; }
            if (errorY >= 0) { current.Y += stepY; errorY -= 2 * dz; }
            errorX += 2 * dx;
            errorY += 2 * dy;
            points.push_back(current);
        }
    }
    return points;
}

// Multi-sample brush geometries delegate expansion to this one helper.
[[nodiscard]] SmartBrushResult ResolveSamples(const SmartToolRequest& request,
    VoxelSnapshot& snapshot, const std::vector<Position>& samples)
{
    SmartBrushResult result;
    std::unordered_set<Position, PositionHash> seenPositions;
    std::unordered_set<Position, PositionHash> seenClipped;
    result.Positions.reserve(samples.size());
    for (const Position sample : samples)
    {
        SmartBrushRequest brushRequest = request.BrushRequest;
        brushRequest.Placement.Target = sample;
        brushRequest.IsOccupied = [&snapshot, &reader = request.ReadVoxel](const Position position)
        {
            SmartToolVoxelState state = reader(position);
            if (!state.Exists) state.PaletteIndex = 0U;
            snapshot.insert_or_assign(position, state);
            return state.Exists;
        };
        const SmartBrushResult brush = SmartBrushEngine::Resolve(brushRequest);
        if (brush.Code != SmartBrushResultCode::Valid &&
            brush.Code != SmartBrushResultCode::OutOfBounds)
            return brush;
        for (const Position position : brush.Positions)
            if (seenPositions.insert(position).second)
                result.Positions.push_back(position);
        for (const Position position : brush.ClippedPositions)
            if (seenClipped.insert(position).second)
                result.ClippedPositions.push_back(position);
    }
    result.Statistics.Total = result.Positions.size() + result.ClippedPositions.size();
    result.AddablePositions.reserve(result.Positions.size());
    result.ExistingPositions.reserve(result.Positions.size());
    for (const Position position : result.Positions)
    {
        const SmartToolVoxelState state = ReadSnapshot(request, snapshot, position);
        if (state.Exists) result.ExistingPositions.push_back(position);
        else result.AddablePositions.push_back(position);
    }
    result.Statistics.New = result.AddablePositions.size();
    result.Statistics.Existing = result.ExistingPositions.size();
    result.Statistics.Clipped = result.ClippedPositions.size();
    if (result.Positions.empty())
    {
        result.Code = SmartBrushResultCode::OutOfBounds;
        return result;
    }
    result.RenderPlan.Mode = SmartBrushRenderMode::DetailedCells;
    result.RenderPlan.Bounds = CalculateBounds(result.Positions);
    result.Code = SmartBrushResultCode::Valid;
    return result;
}

// Geometry Sphere/Cylinder modes use the selected brush mode as their shape,
// not as a second footprint to apply around every already-rasterized sample.
[[nodiscard]] SmartBrushResult ResolveDirectSamples(
    const SmartToolRequest& request, VoxelSnapshot& snapshot,
    const std::vector<Position>& samples)
{
    SmartBrushResult result;
    result.Positions.reserve(samples.size());
    result.ClippedPositions.reserve(samples.size());
    for (const Position position : samples)
    {
        if (IsInside(position, request.BrushRequest.Dimensions))
            result.Positions.push_back(position);
        else
            result.ClippedPositions.push_back(position);
    }
    result.Statistics.Total =
        result.Positions.size() + result.ClippedPositions.size();
    result.AddablePositions.reserve(result.Positions.size());
    result.ExistingPositions.reserve(result.Positions.size());
    for (const Position position : result.Positions)
    {
        const SmartToolVoxelState state =
            ReadSnapshot(request, snapshot, position);
        if (state.Exists) result.ExistingPositions.push_back(position);
        else result.AddablePositions.push_back(position);
    }
    result.Statistics.New = result.AddablePositions.size();
    result.Statistics.Existing = result.ExistingPositions.size();
    result.Statistics.Clipped = result.ClippedPositions.size();
    if (result.Positions.empty())
    {
        result.Code = SmartBrushResultCode::OutOfBounds;
        return result;
    }
    result.RenderPlan.Mode = SmartBrushRenderMode::DetailedCells;
    result.RenderPlan.Bounds = CalculateBounds(result.Positions);
    result.Code = SmartBrushResultCode::Valid;
    return result;
}

[[nodiscard]] SmartBrushResult ResolveLine(const SmartToolRequest& request,
    VoxelSnapshot& snapshot)
{
    if (!request.LineStart)
    {
        SmartBrushResult error;
        error.Code = SmartBrushResultCode::InvalidRequest;
        error.Error = "Line requires a locked start point.";
        return error;
    }
    return ResolveSamples(request, snapshot, RasterizeLine(*request.LineStart,
        request.BrushRequest.Placement.Target));
}

[[nodiscard]] std::int64_t PlaneComponent(
    const Position value, const Position axis) noexcept
{
    return static_cast<std::int64_t>(value.X) * axis.X +
        static_cast<std::int64_t>(value.Y) * axis.Y +
        static_cast<std::int64_t>(value.Z) * axis.Z;
}

[[nodiscard]] std::uint64_t IntegerSquareRoot(
    const std::uint64_t value) noexcept
{
    if (value == 0U) return 0U;
    std::uint64_t low = 1U;
    std::uint64_t high = value;
    std::uint64_t result = 0U;
    while (low <= high)
    {
        const std::uint64_t middle = low + (high - low) / 2U;
        if (middle <= value / middle)
        {
            result = middle;
            low = middle + 1U;
        }
        else high = middle - 1U;
    }
    return result;
}

[[nodiscard]] Position GeometrySample(const SmartToolGeometryPlane& plane,
    const std::int64_t offsetU, const std::int64_t offsetV,
    const std::int64_t offsetNormal)
{
    const std::int64_t x = static_cast<std::int64_t>(plane.Origin.X) +
        offsetU * plane.UAxis.X + offsetV * plane.VAxis.X +
        offsetNormal * plane.Normal.X;
    const std::int64_t y = static_cast<std::int64_t>(plane.Origin.Y) +
        offsetU * plane.UAxis.Y + offsetV * plane.VAxis.Y +
        offsetNormal * plane.Normal.Y;
    const std::int64_t z = static_cast<std::int64_t>(plane.Origin.Z) +
        offsetU * plane.UAxis.Z + offsetV * plane.VAxis.Z +
        offsetNormal * plane.Normal.Z;
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max() ||
        z < std::numeric_limits<std::int32_t>::min() ||
        z > std::numeric_limits<std::int32_t>::max())
        throw std::length_error(
            "Smart Tool Geometry exceeds coordinate limits.");
    return {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z)};
}

[[nodiscard]] std::vector<Position> RasterizeGeometryRectangle(
    const SmartToolGeometryPlane& plane, const Position pointB)
{
    const std::int64_t u = PlaneComponent(pointB, plane.UAxis) -
        PlaneComponent(plane.Origin, plane.UAxis);
    const std::int64_t v = PlaneComponent(pointB, plane.VAxis) -
        PlaneComponent(plane.Origin, plane.VAxis);
    const std::int64_t minU = std::min<std::int64_t>(0, u);
    const std::int64_t maxU = std::max<std::int64_t>(0, u);
    const std::int64_t minV = std::min<std::int64_t>(0, v);
    const std::int64_t maxV = std::max<std::int64_t>(0, v);
    const std::int64_t countU = maxU - minU + 1;
    const std::int64_t countV = maxV - minV + 1;
    if (countU > 1'000'000LL || countV > 1'000'000LL ||
        countU > 1'000'000LL / countV)
        throw std::length_error(
            "Smart Tool Geometry rectangle exceeds the safe sample limit.");
    std::vector<Position> samples;
    samples.reserve(static_cast<std::size_t>(countU * countV));
    for (std::int64_t offsetV = minV; offsetV <= maxV; ++offsetV)
        for (std::int64_t offsetU = minU; offsetU <= maxU; ++offsetU)
        {
            samples.push_back(GeometrySample(plane, offsetU, offsetV, 0));
        }
    return samples;
}

[[nodiscard]] std::vector<Position> RasterizeGeometryDisk(
    const SmartToolGeometryPlane& plane, const Position pointB)
{
    const std::int64_t u = PlaneComponent(pointB, plane.UAxis) -
        PlaneComponent(plane.Origin, plane.UAxis);
    const std::int64_t v = PlaneComponent(pointB, plane.VAxis) -
        PlaneComponent(plane.Origin, plane.VAxis);
    if (std::abs(u) > 1'000'000LL || std::abs(v) > 1'000'000LL)
        throw std::length_error(
            "Smart Tool Geometry disk exceeds the safe radius limit.");
    const std::int64_t radiusSquared = u * u + v * v;
    const std::int64_t radius = static_cast<std::int64_t>(
        IntegerSquareRoot(static_cast<std::uint64_t>(radiusSquared)));
    const std::int64_t diameter = radius * 2 + 1;
    if (diameter > 1'000'000LL ||
        diameter > 1'000'000LL / diameter)
        throw std::length_error(
            "Smart Tool Geometry disk exceeds the safe sample limit.");
    std::vector<Position> samples;
    samples.reserve(static_cast<std::size_t>(diameter * diameter));
    for (std::int64_t offsetV = -radius; offsetV <= radius; ++offsetV)
        for (std::int64_t offsetU = -radius; offsetU <= radius; ++offsetU)
            if (offsetU * offsetU + offsetV * offsetV <= radiusSquared)
                samples.push_back(GeometrySample(
                    plane, offsetU, offsetV, 0));
    return samples;
}

[[nodiscard]] std::vector<Position> RasterizeGeometryCylinder(
    const SmartToolGeometryPlane& plane, const Position pointB,
    const int signedHeight)
{
    if (signedHeight == 0)
        throw std::invalid_argument(
            "Smart Tool Geometry cylinder requires a signed height.");
    std::vector<Position> disk = RasterizeGeometryDisk(plane, pointB);
    const std::int64_t layerCount = std::abs(
        static_cast<std::int64_t>(signedHeight));
    if (layerCount > 1'000'000LL ||
        (!disk.empty() &&
         layerCount > 1'000'000LL / static_cast<std::int64_t>(disk.size())))
        throw std::length_error(
            "Smart Tool Geometry cylinder exceeds the safe sample limit.");
    std::vector<Position> samples;
    samples.reserve(disk.size() * static_cast<std::size_t>(layerCount));
    const std::int64_t direction = signedHeight > 0 ? 1 : -1;
    for (std::int64_t layer = 0; layer < layerCount; ++layer)
        for (const Position position : disk)
        {
            SmartToolGeometryPlane translated = plane;
            translated.Origin = position;
            samples.push_back(GeometrySample(
                translated, 0, 0, direction * layer));
        }
    return samples;
}

[[nodiscard]] SmartBrushResult ResolveGeometry(const SmartToolRequest& request,
    VoxelSnapshot& snapshot)
{
    if (!request.GeometryPlane)
    {
        SmartBrushResult error;
        error.Code = SmartBrushResultCode::InvalidRequest;
        error.Error = "Geometry requires a locked plane and first point.";
        return error;
    }
    if (!request.Mode)
    {
        SmartBrushResult error;
        error.Code = SmartBrushResultCode::InvalidRequest;
        error.Error = "Geometry requires an explicit Smart Tool mode.";
        return error;
    }
    switch (*request.Mode)
    {
    case SmartToolMode::SingleVoxel:
    case SmartToolMode::CubeBrush:
        return ResolveSamples(request, snapshot, RasterizeGeometryRectangle(
            *request.GeometryPlane, request.BrushRequest.Placement.Target));
    case SmartToolMode::SphereBrush:
        return ResolveDirectSamples(request, snapshot, RasterizeGeometryDisk(
            *request.GeometryPlane, request.BrushRequest.Placement.Target));
    case SmartToolMode::CylinderBrush:
        if (request.GeometryHeight == 0)
        {
            SmartBrushResult error;
            error.Code = SmartBrushResultCode::InvalidRequest;
            error.Error =
                "Geometry Cylinder requires a non-zero signed height.";
            return error;
        }
        return ResolveDirectSamples(request, snapshot,
            RasterizeGeometryCylinder(*request.GeometryPlane,
                request.BrushRequest.Placement.Target,
                request.GeometryHeight));
    default:
        SmartBrushResult error;
        error.Code = SmartBrushResultCode::InvalidRequest;
        error.Error = "Geometry requires a supported Smart Tool mode.";
        return error;
    }
}

[[nodiscard]] SmartBrushResult FillError(std::string message)
{
    SmartBrushResult result;
    result.Code = SmartBrushResultCode::InvalidRequest;
    result.Error = std::move(message);
    return result;
}

[[nodiscard]] SmartBrushResult ResolveFill(const SmartToolRequest& request,
    VoxelSnapshot& snapshot, const std::size_t fillCellLimit)
{
    const Position seed = request.BrushRequest.Placement.Target;
    if (!IsInside(seed, request.BrushRequest.Dimensions))
        return FillError("Fill requires a target inside the active model.");
    const SmartToolVoxelState source = ReadSnapshot(request, snapshot, seed);
    if (!source.Exists)
        return FillError("Fill requires an existing source voxel.");

    Position normal{};
    std::array<Position, 6U> connectedOffsets{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::span<const Position> offsets{connectedOffsets};
    std::array<Position, 4U> planeOffsets{};
    if (request.FillMode == SmartFillMode::Plane)
    {
        if (!request.FaceSeed || request.FaceSeed->Position != seed ||
            !IsUnitAxisNormal(request.FaceSeed->Normal))
            return FillError(
                "Plane Fill requires a locked visible voxel face.");
        normal = request.FaceSeed->Normal;
        const std::optional<Position> outward = Offset(seed, normal);
        if (outward && IsInside(*outward, request.BrushRequest.Dimensions) &&
            ReadSnapshot(request, snapshot, *outward).Exists)
            return FillError(
                "Plane Fill requires a locked visible voxel face.");
        planeOffsets = PlanarOffsets(normal);
        offsets = planeOffsets;
    }

    const auto belongsToRegion = [&](const Position position)
    {
        if (!IsInside(position, request.BrushRequest.Dimensions))
            return false;
        const SmartToolVoxelState state =
            ReadSnapshot(request, snapshot, position);
        return state.Exists && state.PaletteIndex == source.PaletteIndex;
    };

    std::deque<Position> pending;
    std::unordered_set<Position, PositionHash> visited;
    std::vector<Position> region;
    pending.push_back(seed);
    visited.insert(seed);
    while (!pending.empty())
    {
        const Position current = pending.front();
        pending.pop_front();
        region.push_back(current);
        for (const Position delta : offsets)
        {
            const std::optional<Position> neighbor = Offset(current, delta);
            if (!neighbor || !visited.insert(*neighbor).second ||
                !belongsToRegion(*neighbor))
                continue;
            if (region.size() + pending.size() >= fillCellLimit)
                return FillError(
                    "Fill region exceeds the 1,000,000 cell safety limit.");
            pending.push_back(*neighbor);
        }
    }

    SmartBrushResult result;
    result.Positions.reserve(region.size());
    if (request.FillMode == SmartFillMode::Plane &&
        request.Action == SmartAction::Add)
    {
        for (const Position sourcePosition : region)
        {
            const std::optional<Position> destination =
                Offset(sourcePosition, normal);
            if (!destination ||
                !IsInside(*destination, request.BrushRequest.Dimensions))
                continue;
            if (!ReadSnapshot(request, snapshot, *destination).Exists)
                result.Positions.push_back(*destination);
        }
    }
    else
    {
        result.Positions = std::move(region);
    }

    result.Statistics.Total = result.Positions.size();
    result.AddablePositions.reserve(result.Positions.size());
    result.ExistingPositions.reserve(result.Positions.size());
    for (const Position position : result.Positions)
    {
        if (ReadSnapshot(request, snapshot, position).Exists)
            result.ExistingPositions.push_back(position);
        else
            result.AddablePositions.push_back(position);
    }
    result.Statistics.New = result.AddablePositions.size();
    result.Statistics.Existing = result.ExistingPositions.size();
    result.RenderPlan.Mode = SmartBrushRenderMode::DetailedCells;
    result.RenderPlan.Bounds = CalculateBounds(result.Positions);
    result.Code = SmartBrushResultCode::Valid;
    return result;
}

[[nodiscard]] SmartBrushResult ResolveSurfaceFootprint(
    const SmartToolRequest& request, VoxelSnapshot& snapshot)
{
    SmartBrushRequest brushRequest = request.BrushRequest;
    brushRequest.State.Dimension = SmartBrushDimension::Surface2D;
    brushRequest.State.Orientation = SmartBrushOrientation::Auto;
    brushRequest.IsOccupied = [&request, &snapshot](const Position position)
    {
        return ReadSnapshot(request, snapshot, position).Exists;
    };
    return SmartBrushEngine::Resolve(brushRequest);
}

void ClassifySurfacePositions(SmartBrushResult& result,
    const SmartToolRequest& request, VoxelSnapshot& snapshot)
{
    result.AddablePositions.clear();
    result.ExistingPositions.clear();
    result.AddablePositions.reserve(result.Positions.size());
    result.ExistingPositions.reserve(result.Positions.size());
    for (const Position position : result.Positions)
    {
        if (ReadSnapshot(request, snapshot, position).Exists)
        {
            result.ExistingPositions.push_back(position);
        }
        else
        {
            result.AddablePositions.push_back(position);
        }
    }
    result.Statistics.Total = result.Positions.size() +
        result.ClippedPositions.size();
    result.Statistics.New = result.AddablePositions.size();
    result.Statistics.Existing = result.ExistingPositions.size();
    result.Statistics.Clipped = result.ClippedPositions.size();
    if (result.Positions.empty())
    {
        result.Code = result.ClippedPositions.empty()
            ? SmartBrushResultCode::Valid
            : SmartBrushResultCode::OutOfBounds;
        return;
    }
    result.RenderPlan.Mode = SmartBrushRenderMode::DetailedCells;
    result.RenderPlan.Bounds = CalculateBounds(result.Positions);
    result.Code = SmartBrushResultCode::Valid;
}

[[nodiscard]] SmartBrushResult ResolveSurface(const SmartToolRequest& request,
    VoxelSnapshot& snapshot)
{
    // The Face resolver remains the one authority for finding a complete,
    // exposed, 4-connected source component. Surface changes only what is
    // done with that locked component; it never reimplements the flood fill.
    SmartToolRequest sourceRequest = request;
    sourceRequest.Action = SmartAction::Paint;
    SmartBrushResult sourceSurface = ResolveFace(sourceRequest, snapshot);
    if (sourceSurface.Code != SmartBrushResultCode::Valid)
    {
        return sourceSurface;
    }

    std::unordered_set<Position, PositionHash> lockedSurface;
    lockedSurface.reserve(sourceSurface.Positions.size());
    for (const Position position : sourceSurface.Positions)
    {
        lockedSurface.insert(position);
    }

    // Paint is intentionally a one-gesture operation on the full locked
    // component. Pointer motion cannot turn it into a partial or rear-layer
    // paint operation.
    if (request.Action == SmartAction::Paint)
    {
        ClassifySurfacePositions(sourceSurface, request, snapshot);
        return sourceSurface;
    }

    SmartBrushResult footprint = ResolveSurfaceFootprint(request, snapshot);
    if (footprint.Code != SmartBrushResultCode::Valid &&
        footprint.Code != SmartBrushResultCode::OutOfBounds)
    {
        return footprint;
    }

    SmartBrushResult result;
    result.ClippedPositions = std::move(footprint.ClippedPositions);
    if (request.Action == SmartAction::Erase)
    {
        // Remove can never step behind the captured surface: only positions
        // in the original exposed component are made available to the plan.
        for (const Position position : footprint.Positions)
        {
            if (lockedSurface.contains(position))
            {
                result.Positions.push_back(position);
            }
        }
        ClassifySurfacePositions(result, request, snapshot);
        return result;
    }

    if (request.Action != SmartAction::Add)
    {
        result.Code = SmartBrushResultCode::Unsupported;
        result.Error = "Surface supports Add, Remove, and Paint only.";
        return result;
    }

    std::unordered_set<Position, PositionHash> candidates;
    candidates.reserve(footprint.Positions.size());
    for (const Position position : footprint.Positions)
    {
        // Existing unrelated document voxels cannot connect a detached island.
        // Earlier Add cells are exposed separately by the stroke callback.
        const bool extension = request.ReadSurfaceExtensionVoxel &&
            request.ReadSurfaceExtensionVoxel(position).Exists;
        if (!extension && !ReadSnapshot(request, snapshot, position).Exists)
        {
            candidates.insert(position);
        }
    }

    const auto isAnchor = [&lockedSurface, &request](const Position position)
    {
        return lockedSurface.contains(position) ||
            (request.ReadSurfaceExtensionVoxel &&
             request.ReadSurfaceExtensionVoxel(position).Exists);
    };
    const Position normal = request.FaceSeed->Normal;
    const std::array<Position, 4U> offsets = PlanarOffsets(normal);
    std::deque<Position> pending;
    std::unordered_set<Position, PositionHash> accepted;
    accepted.reserve(candidates.size());
    for (const Position candidate : candidates)
    {
        for (const Position offset : offsets)
        {
            const std::optional<Position> neighbor = Offset(candidate, offset);
            if (neighbor && isAnchor(*neighbor))
            {
                accepted.insert(candidate);
                pending.push_back(candidate);
                break;
            }
        }
    }
    while (!pending.empty())
    {
        const Position current = pending.front();
        pending.pop_front();
        for (const Position offset : offsets)
        {
            const std::optional<Position> neighbor = Offset(current, offset);
            if (neighbor && candidates.contains(*neighbor) &&
                accepted.insert(*neighbor).second)
            {
                pending.push_back(*neighbor);
            }
        }
    }
    for (const Position position : footprint.Positions)
    {
        if (accepted.contains(position))
        {
            result.Positions.push_back(position);
        }
    }
    ClassifySurfacePositions(result, request, snapshot);
    return result;
}

[[nodiscard]] SmartBrushMode ModeForAction(const SmartAction action) noexcept
{
    switch (action)
    {
    case SmartAction::Erase: return SmartBrushMode::Erase;
    case SmartAction::Paint: return SmartBrushMode::Paint;
    case SmartAction::Replace: return SmartBrushMode::Replace;
    case SmartAction::Add:
    default: return SmartBrushMode::Add;
    }
}

[[nodiscard]] Asset::Voxel::VoxelPosition LocalPosition(
    const Asset::Voxel::VoxelPosition world,
    const Asset::Voxel::VoxelPosition origin) noexcept
{
    return {world.X - origin.X, world.Y - origin.Y, world.Z - origin.Z};
}

[[nodiscard]] SmartToolPlanCell BuildResolvedCell(
    const std::size_t sourceOrdinal,
    const Asset::Voxel::VoxelPosition position,
    const SmartToolRequest& request,
    SmartToolVoxelState before)
{
    if (!before.Exists) before.PaletteIndex = 0U;
    SmartToolVoxelState after = before;
    switch (request.Action)
    {
    case SmartAction::Add:
        if (!before.Exists)
            after = {true, static_cast<std::uint8_t>(
                request.BrushRequest.State.PaletteIndex)};
        break;
    case SmartAction::Erase:
        if (before.Exists) after = {};
        break;
    case SmartAction::Paint:
        if (before.Exists)
            after = {true, static_cast<std::uint8_t>(
                request.BrushRequest.State.PaletteIndex)};
        break;
    case SmartAction::Replace:
        if (before.Exists && request.ReplacePaletteIndex &&
            before.PaletteIndex == *request.ReplacePaletteIndex)
        {
            after = {true, static_cast<std::uint8_t>(
                request.BrushRequest.State.PaletteIndex)};
        }
        break;
    default: break;
    }

    SmartToolCellOperation operation = SmartToolCellOperation::Ignore;
    SmartToolPlanPreviewState preview = SmartToolPlanPreviewState::Ignored;
    SmartToolPlanCellDiagnostic diagnostic =
        SmartToolPlanCellDiagnostic::NoChange;
    SmartToolPlanCellFlag flags = SmartToolPlanCellFlag::None;
    if (before.Exists) flags |= SmartToolPlanCellFlag::ExistingVoxel;
    if (after.Exists) flags |= SmartToolPlanCellFlag::FinalVoxel;
    if (request.Action == SmartAction::Add && before.Exists)
    {
        flags |= SmartToolPlanCellFlag::Overlap;
        diagnostic = SmartToolPlanCellDiagnostic::Overlap;
    }

    if (before != after)
    {
        diagnostic = SmartToolPlanCellDiagnostic::None;
        if (!before.Exists && after.Exists)
        {
            operation = SmartToolCellOperation::Add;
            preview = SmartToolPlanPreviewState::Added;
        }
        else if (before.Exists && !after.Exists)
        {
            operation = SmartToolCellOperation::Erase;
            preview = SmartToolPlanPreviewState::Erased;
        }
        else
        {
            operation = request.Action == SmartAction::Replace
                ? SmartToolCellOperation::Replace
                : SmartToolCellOperation::Paint;
            preview = request.Action == SmartAction::Replace
                ? SmartToolPlanPreviewState::Replaced
                : SmartToolPlanPreviewState::Painted;
        }
    }
    else flags |= SmartToolPlanCellFlag::NoChange;

    return {sourceOrdinal,
        LocalPosition(position, request.BrushRequest.Placement.Target),
        position, before, after, request.Action, operation, preview, diagnostic,
        flags};
}

[[nodiscard]] SmartToolPlanCell BuildClippedCell(
    const std::size_t sourceOrdinal,
    const Asset::Voxel::VoxelPosition position,
    const SmartToolRequest& request) noexcept
{
    return {sourceOrdinal,
        LocalPosition(position, request.BrushRequest.Placement.Target),
        position, {}, {}, request.Action, SmartToolCellOperation::Ignore,
        SmartToolPlanPreviewState::Clipped,
        SmartToolPlanCellDiagnostic::OutOfBounds,
        SmartToolPlanCellFlag::OutOfBounds |
            SmartToolPlanCellFlag::NoChange};
}

[[nodiscard]] std::vector<SmartToolPlanCell> BuildCells(
    const SmartBrushResult& brush,
    const SmartToolRequest& request,
    const VoxelSnapshot& snapshot)
{
    std::vector<SmartToolPlanCell> cells;
    cells.reserve(brush.Positions.size() + brush.ClippedPositions.size());
    std::size_t ordinal = 0U;
    for (const Asset::Voxel::VoxelPosition position : brush.Positions)
    {
        const auto value = snapshot.find(position);
        if (value == snapshot.end())
        {
            cells.push_back({ordinal++,
                LocalPosition(position, request.BrushRequest.Placement.Target),
                position, {}, {}, request.Action,
                SmartToolCellOperation::Ignore,
                SmartToolPlanPreviewState::Invalid,
                SmartToolPlanCellDiagnostic::InvalidState,
                SmartToolPlanCellFlag::Invalid |
                    SmartToolPlanCellFlag::NoChange});
            continue;
        }
        cells.push_back(BuildResolvedCell(
            ordinal++, position, request, value->second));
    }
    for (const Asset::Voxel::VoxelPosition position : brush.ClippedPositions)
        cells.push_back(BuildClippedCell(ordinal++, position, request));
    return cells;
}

[[nodiscard]] SmartToolResult Error(
    const SmartBrushResultCode code, std::string error)
{
    return {SmartToolStatusFrom(code), code, nullptr, std::move(error)};
}
}

std::optional<SmartToolGeometryPlane> SmartToolPlanner::MakeGeometryPlane(
    const Position origin, const Position normal,
    const float surfaceCoordinate) noexcept
{
    if (!IsUnitAxisNormal(normal) || !std::isfinite(surfaceCoordinate))
        return std::nullopt;
    if (normal.X != 0)
        return SmartToolGeometryPlane{origin, normal, {0, 1, 0}, {0, 0, 1},
            surfaceCoordinate};
    if (normal.Y != 0)
        return SmartToolGeometryPlane{origin, normal, {1, 0, 0}, {0, 0, 1},
            surfaceCoordinate};
    return SmartToolGeometryPlane{origin, normal, {1, 0, 0}, {0, 1, 0},
        surfaceCoordinate};
}

Asset::Voxel::VoxelPosition SmartToolPlanner::ProjectGeometryEndpoint(
    const SmartToolGeometryPlane& plane, Position endpoint) noexcept
{
    // The normal is a signed unit axis. Retaining Origin on it locks the
    // interaction plane even while the pointer hovers another surface.
    if (plane.Normal.X != 0) endpoint.X = static_cast<std::int32_t>(
        std::floor(plane.SurfaceCoordinate));
    else if (plane.Normal.Y != 0) endpoint.Y = static_cast<std::int32_t>(
        std::floor(plane.SurfaceCoordinate));
    else if (plane.Normal.Z != 0) endpoint.Z = static_cast<std::int32_t>(
        std::floor(plane.SurfaceCoordinate));
    return endpoint;
}

PencilCompactPlanResult SmartToolPlanner::PlanPencilCompact(
    const PencilCompactRequest& request)
{
    const bool supportedAction = request.Action == SmartAction::Add ||
        request.Action == SmartAction::Erase ||
        request.Action == SmartAction::Paint;
    const bool paletteRequired = request.Action == SmartAction::Add ||
        request.Action == SmartAction::Paint;
    const bool knownBrushMode = request.Brush.Mode == SmartBrushMode::Add ||
        request.Brush.Mode == SmartBrushMode::Erase ||
        request.Brush.Mode == SmartBrushMode::Paint ||
        request.Brush.Mode == SmartBrushMode::Replace;
    if (request.Dimensions.X == 0U || request.Dimensions.Y == 0U ||
        request.Dimensions.Z == 0U)
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact requires non-zero document dimensions."};
    }
    if (!IsUnitAxisNormal(request.Placement.Normal))
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact requires a unit axis placement normal."};
    }
    if (!supportedAction)
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact supports Add, Erase, and Paint only."};
    }
    if (paletteRequired && (request.PaletteIndex == 0U ||
        request.PaletteIndex > std::numeric_limits<std::uint8_t>::max()))
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact Add and Paint require a palette index from 1 to 255."};
    }
    if (!knownBrushMode || request.Brush.PreviewMode !=
            SmartBrushPreviewMode::Adaptive)
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact received an unsupported brush state."};
    }
    SmartBrushCompactDescriptor descriptor;
    descriptor.Shape = request.Brush.Shape;
    descriptor.Dimension = request.Brush.Dimension;
    descriptor.Orientation = request.Brush.Dimension ==
            SmartBrushDimension::Surface2D
        ? ResolveCompactOrientation(request.Brush.Orientation,
            request.Placement.Normal)
        : SmartBrushOrientation::Y;
    descriptor.Size = request.Brush.Size;
    descriptor.ProfileIdentity = request.ProfileIdentity;
    descriptor.ProfileRevision = request.ProfileRevision;
    if (!SmartBrushCompactFootprint::IsValid(descriptor))
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact requires Cube, Sphere, or Cylinder with a size between 1 and 256."};
    }
    const std::optional<Position> anchor = descriptor.Dimension ==
            SmartBrushDimension::Volume3D
        ? CompactVolumeAnchor(request.Placement, descriptor.Size)
        : std::optional<Position>{request.Placement.Target};
    if (!anchor)
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact anchor exceeds voxel coordinate limits."};
    }

    const SmartBrushCompactCacheKey cacheKey{descriptor};
    std::shared_ptr<const SmartBrushCompactFootprint> footprint;
    const auto found = pencilCompactFootprints_.find(cacheKey);
    if (found != pencilCompactFootprints_.end()) footprint = found->second;
    else
    {
        // A fixed, small cache is intentional: it caches scalar procedural
        // descriptors only and cannot grow with pointer movement or strokes.
        if (pencilCompactFootprints_.size() >= 16U)
            pencilCompactFootprints_.clear();
        try
        {
            footprint = std::make_shared<SmartBrushCompactFootprint>(
                SmartBrushCompactFootprint::Create(descriptor));
        }
        catch (const std::exception& exception)
        {
            return {PencilCompactPlanCode::InvalidRequest, nullptr,
                exception.what()};
        }
        pencilCompactFootprints_.emplace(cacheKey, footprint);
    }
    const std::optional<SmartBrushBounds> bounds = TranslateCompactBounds(
        footprint->LocalBounds(), *anchor);
    if (!bounds)
    {
        return {PencilCompactPlanCode::InvalidRequest, nullptr,
            "Pencil Compact bounds exceed voxel coordinate limits."};
    }
    const bool insideDocument = IsCompactBoundsInside(*bounds, request.Dimensions);
    const PencilCompactPlanPtr plan(new PencilCompactPlan(cacheKey,
        std::move(footprint), request.Placement, *anchor, *bounds,
        nextPencilCompactPlanId_++, insideDocument, request.Action,
        request.PaletteIndex, request.DocumentGeneration, request.DocumentRevision));
    return {insideDocument ? PencilCompactPlanCode::Valid :
            PencilCompactPlanCode::OutOfBounds,
        plan, insideDocument ? std::string{} :
            "Pencil Compact footprint extends outside the document."};
}

std::vector<PencilCompactPlanResult> SmartToolPlanner::PlanPencilCompactBatch(
    const PencilCompactRequest& request,
    const std::span<const Position> centres)
{
    std::vector<PencilCompactPlanResult> results;
    results.reserve(centres.size());
    for (const Position centre : centres)
    {
        PencilCompactRequest centredRequest = request;
        centredRequest.Placement.Target = centre;
        results.push_back(PlanPencilCompact(centredRequest));
    }
    return results;
}

std::size_t SmartToolPlanner::PencilCompactFootprintCacheSize() const noexcept
{
    return pencilCompactFootprints_.size();
}

SmartToolResult SmartToolPlanner::Plan(const SmartToolRequest& request)
{
    if (request.SourceIdentity == 0U)
    {
        return Error(SmartBrushResultCode::InvalidRequest,
            "SMART-01 requires an identified source document.");
    }
    if (request.Geometry != SmartGeometry::Pencil &&
        request.Geometry != SmartGeometry::Cube &&
        request.Geometry != SmartGeometry::Sphere &&
        request.Geometry != SmartGeometry::Face &&
        request.Geometry != SmartGeometry::Line &&
        request.Geometry != SmartGeometry::Geometry &&
        request.Geometry != SmartGeometry::Surface &&
        request.Geometry != SmartGeometry::Fill)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "The Smart Tool planner supports only Pencil, Cube, Sphere, Face, Line, Geometry, Surface, and Fill geometry.");
    }
    if (request.Mode &&
        request.Geometry != SmartGeometry::Pencil &&
        request.Geometry != SmartGeometry::Line &&
        request.Geometry != SmartGeometry::Geometry &&
        request.Geometry != SmartGeometry::Surface)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "Smart Tool brush modes require Pencil, Line, Geometry, or Surface geometry.");
    }
    if (request.Action != SmartAction::Add &&
        request.Action != SmartAction::Erase &&
        request.Action != SmartAction::Paint &&
        request.Action != SmartAction::Replace)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "The Smart Tool planner does not support this action.");
    }
    if (!request.ReadVoxel)
    {
        return Error(SmartBrushResultCode::InvalidRequest,
            "The Smart Tool request requires a versioned voxel reader.");
    }
    if ((request.Action == SmartAction::Add ||
         request.Action == SmartAction::Paint ||
         request.Action == SmartAction::Replace) &&
        (request.BrushRequest.State.PaletteIndex == 0U ||
         request.BrushRequest.State.PaletteIndex > 255U))
    {
        return Error(SmartBrushResultCode::InvalidRequest,
            "The Smart Tool target palette is invalid.");
    }
    if (request.Action == SmartAction::Replace &&
        !request.ReplacePaletteIndex)
    {
        return Error(SmartBrushResultCode::InvalidRequest,
            "Replace requires a source palette snapshot.");
    }

    try
    {
        SmartToolRequest normalized = request;
        if (request.Mode)
        {
            // The Planner is the business boundary that resolves a Smart Tool
            // mode. Preview and Commit subsequently consume this exact plan.
            if (request.Geometry == SmartGeometry::Pencil)
                normalized.Geometry = SmartGeometry::Pencil;
            // Pencil deliberately preserves its selected 3D/2D brush mode.
            // V1 resolves Pencil Surface2D from the placement face/workplane;
            // a fixed axis carried by a legacy brush profile must not override
            // that face. This planner remains the one source of final
            // geometry for both preview and commit.
            // Existing specialised geometries keep their established fixed
            // normalisation.
            if (request.Geometry == SmartGeometry::Pencil)
            {
                normalized.BrushRequest.State.Orientation = SmartBrushOrientation::Auto;
            }
            else
            {
                normalized.BrushRequest.State.Dimension =
                    request.Geometry == SmartGeometry::Surface
                    ? SmartBrushDimension::Surface2D
                    : SmartBrushDimension::Volume3D;
                normalized.BrushRequest.State.Orientation = SmartBrushOrientation::Auto;
            }
            switch (*request.Mode)
            {
            case SmartToolMode::SingleVoxel:
                normalized.BrushRequest.State.Shape = SmartBrushShape::Cube;
                normalized.BrushRequest.State.Size = 1;
                break;
            case SmartToolMode::CubeBrush:
                normalized.BrushRequest.State.Shape = SmartBrushShape::Cube;
                break;
            case SmartToolMode::SphereBrush:
                normalized.BrushRequest.State.Shape = SmartBrushShape::Sphere;
                break;
            case SmartToolMode::CylinderBrush:
                normalized.BrushRequest.State.Shape = SmartBrushShape::Cylinder;
                break;
            default:
                return Error(SmartBrushResultCode::Unsupported,
                    "The requested Smart Tool mode is not implemented.");
            }
            if (*request.Mode != SmartToolMode::SingleVoxel)
            {
                if (normalized.BrushRequest.State.Size < 1 ||
                    normalized.BrushRequest.State.Size > MaximumSmartToolBrushSize)
                    return Error(SmartBrushResultCode::InvalidRequest,
                        "Smart Brush size must be between 1 and 64 voxels.");
            }
            normalized.BrushRequest.MaximumSize = MaximumSmartToolBrushSize;
        }
        else normalized.BrushRequest.State.Shape = ResolveSmartBrushShape(
            request.Geometry, normalized.BrushRequest.State.Shape);
        normalized.BrushRequest.State.Mode = ModeForAction(request.Action);

        VoxelSnapshot snapshot;
        SmartBrushRequest geometryRequest = normalized.BrushRequest;
        // Replace uses Paint geometry; the action decision remains exclusively
        // in BuildResolvedCell and is fully captured in the final plan.
        if (request.Action == SmartAction::Replace)
            geometryRequest.State.Mode = SmartBrushMode::Paint;
        geometryRequest.IsOccupied =
            [&snapshot, &reader = normalized.ReadVoxel](
                const Asset::Voxel::VoxelPosition position)
            {
                SmartToolVoxelState state = reader(position);
                if (!state.Exists) state.PaletteIndex = 0U;
                snapshot.insert_or_assign(position, state);
                return state.Exists;
            };

        SmartBrushResult brush;
        if (request.Geometry == SmartGeometry::Face)
        {
            brush = ResolveFace(normalized, snapshot);
        }
        else if (request.Geometry == SmartGeometry::Line)
        {
            brush = ResolveLine(normalized, snapshot);
        }
        else if (request.Geometry == SmartGeometry::Geometry ||
                 request.Geometry == SmartGeometry::Surface ||
                 request.Geometry == SmartGeometry::Fill)
        {
            brush = request.Geometry == SmartGeometry::Surface
                ? ResolveSurface(normalized, snapshot)
                : request.Geometry == SmartGeometry::Fill
                ? ResolveFill(normalized, snapshot, fillCellLimit_)
                : ResolveGeometry(normalized, snapshot);
        }
        else
        {
            brush = SmartBrushEngine::Resolve(geometryRequest);
        }
        if ((request.Geometry == SmartGeometry::Face ||
                request.Geometry == SmartGeometry::Line ||
                request.Geometry == SmartGeometry::Geometry ||
                request.Geometry == SmartGeometry::Surface ||
                request.Geometry == SmartGeometry::Fill) &&
            brush.Code == SmartBrushResultCode::InvalidRequest)
            return Error(brush.Code, std::move(brush.Error));
        const SmartBrushResultCode code = brush.Code;
        const std::string error = brush.Error;
        std::vector<SmartToolDiagnostic> diagnostics;
        if (!error.empty())
            diagnostics.push_back({SmartToolStatusFrom(code), error});
        SmartToolRequest cellRequest = normalized;
        // Connected Create is deliberately the same region recolour as Paint.
        // The immutable Plan retains the user's Add action while its cells
        // capture the exact paint operations consumed by preview and commit.
        if (normalized.Geometry == SmartGeometry::Fill &&
            normalized.FillMode == SmartFillMode::Connected &&
            normalized.Action == SmartAction::Add)
            cellRequest.Action = SmartAction::Paint;
        std::vector<SmartToolPlanCell> cells =
            BuildCells(brush, cellRequest, snapshot);
        const std::uint64_t planId = nextPlanId_++;
        const SmartToolPlanPtr plan(new SmartToolPlan(
            normalized, std::move(brush), planId, std::move(cells),
            std::move(diagnostics)));
        return {SmartToolStatusFrom(code), code, plan, error};
    }
    catch (const std::exception& exception)
    {
        return Error(
            SmartBrushResultCode::TechnicalFailure, exception.what());
    }
    catch (...)
    {
        return Error(SmartBrushResultCode::TechnicalFailure,
            "Smart Tool planning failed.");
    }
}
} // namespace VoxelForge::Editor
