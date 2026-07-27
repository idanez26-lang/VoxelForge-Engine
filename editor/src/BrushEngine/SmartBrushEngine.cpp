#include "BrushEngine/SmartBrushEngine.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
bool IsSupportedShape(const SmartBrushShape shape) noexcept
{
    return shape == SmartBrushShape::Cube || shape == SmartBrushShape::Sphere ||
        shape == SmartBrushShape::Cylinder;
}

bool IsKnownDimension(const SmartBrushDimension dimension) noexcept
{
    return dimension == SmartBrushDimension::Volume3D ||
        dimension == SmartBrushDimension::Surface2D;
}

bool IsKnownOrientation(const SmartBrushOrientation orientation) noexcept
{
    return orientation == SmartBrushOrientation::Auto ||
        orientation == SmartBrushOrientation::X ||
        orientation == SmartBrushOrientation::Y ||
        orientation == SmartBrushOrientation::Z;
}

bool IsKnownMode(const SmartBrushMode mode) noexcept
{
    return mode == SmartBrushMode::Add || mode == SmartBrushMode::Erase ||
        mode == SmartBrushMode::Paint;
}

bool IsInside(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions.X &&
        static_cast<std::uint32_t>(position.Y) < dimensions.Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions.Z;
}

SmartBrushOrientation ResolveOrientation(
    const SmartBrushOrientation orientation,
    const Asset::Voxel::VoxelPosition normal) noexcept
{
    if (orientation != SmartBrushOrientation::Auto) return orientation;
    const int absX = std::abs(normal.X);
    const int absY = std::abs(normal.Y);
    const int absZ = std::abs(normal.Z);
    if (absX == 0 && absY == 0 && absZ == 0)
        return SmartBrushOrientation::Y;
    if (absX >= absY && absX >= absZ) return SmartBrushOrientation::X;
    if (absZ >= absY) return SmartBrushOrientation::Z;
    return SmartBrushOrientation::Y;
}

std::vector<Asset::Voxel::VoxelPosition> GenerateSurface(
    const SmartBrushPlacement placement,
    const SmartBrushShape shape,
    const SmartBrushOrientation orientation,
    const int size)
{
    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    const int radiusSquared = size * size;
    const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(static_cast<std::size_t>(size) *
        static_cast<std::size_t>(size));
    for (int second = minimumOffset; second <= maximumOffset; ++second)
    {
        for (int first = minimumOffset; first <= maximumOffset; ++first)
        {
            if (shape == SmartBrushShape::Sphere)
            {
                const int dx = 2 * first - evenCenterOffset;
                const int dy = 2 * second - evenCenterOffset;
                if (dx * dx + dy * dy > radiusSquared) continue;
            }
            Asset::Voxel::VoxelPosition position = placement.Target;
            switch (orientation)
            {
            case SmartBrushOrientation::X:
                position.Y += first;
                position.Z += second;
                break;
            case SmartBrushOrientation::Y:
                position.X += first;
                position.Z += second;
                break;
            case SmartBrushOrientation::Z:
                position.X += first;
                position.Y += second;
                break;
            case SmartBrushOrientation::Auto: break;
            }
            positions.push_back(position);
        }
    }
    return positions;
}

SmartBrushBounds CalculateBounds(
    const std::vector<Asset::Voxel::VoxelPosition>& positions)
{
    SmartBrushBounds bounds{positions.front(), positions.front()};
    for (const Asset::Voxel::VoxelPosition position : positions)
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

[[nodiscard]] Asset::Voxel::VoxelPosition VolumeAnchor(
    const SmartBrushPlacement placement, const int size) noexcept
{
    const int depthOffset = (size - 1) / 2;
    return {placement.Target.X + placement.Normal.X * depthOffset,
        placement.Target.Y + placement.Normal.Y * depthOffset,
        placement.Target.Z + placement.Normal.Z * depthOffset};
}

[[nodiscard]] std::vector<Asset::Voxel::VoxelPosition> GenerateCubeVolume(
    const SmartBrushPlacement placement, const int size)
{
    const Asset::Voxel::VoxelPosition anchor = VolumeAnchor(placement, size);
    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(static_cast<std::size_t>(size) *
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int z = minimumOffset; z <= maximumOffset; ++z)
        for (int y = minimumOffset; y <= maximumOffset; ++y)
            for (int x = minimumOffset; x <= maximumOffset; ++x)
                positions.push_back({anchor.X + x, anchor.Y + y, anchor.Z + z});
    return positions;
}

[[nodiscard]] std::vector<Asset::Voxel::VoxelPosition> GenerateSphereVolume(
    const SmartBrushPlacement placement, const int size)
{
    const Asset::Voxel::VoxelPosition anchor = VolumeAnchor(placement, size);
    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
    const int radiusSquared = size * size;
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(static_cast<std::size_t>(size) *
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int z = minimumOffset; z <= maximumOffset; ++z)
    {
        for (int y = minimumOffset; y <= maximumOffset; ++y)
        {
            for (int x = minimumOffset; x <= maximumOffset; ++x)
            {
                const int dx = 2 * x - evenCenterOffset;
                const int dy = 2 * y - evenCenterOffset;
                const int dz = 2 * z - evenCenterOffset;
                if (dx * dx + dy * dy + dz * dz > radiusSquared) continue;
                positions.push_back({anchor.X + x, anchor.Y + y, anchor.Z + z});
            }
        }
    }
    return positions;
}

[[nodiscard]] std::vector<Asset::Voxel::VoxelPosition>
GenerateVerticalCylinderVolume(const SmartBrushPlacement placement, const int size)
{
    const Asset::Voxel::VoxelPosition anchor = VolumeAnchor(placement, size);
    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
    const int radiusSquared = size * size;
    std::vector<Asset::Voxel::VoxelPosition> positions;
    positions.reserve(static_cast<std::size_t>(size) *
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (int y = minimumOffset; y <= maximumOffset; ++y)
    {
        for (int z = minimumOffset; z <= maximumOffset; ++z)
        {
            for (int x = minimumOffset; x <= maximumOffset; ++x)
            {
                const int dx = 2 * x - evenCenterOffset;
                const int dz = 2 * z - evenCenterOffset;
                if (dx * dx + dz * dz > radiusSquared) continue;
                positions.push_back({anchor.X + x, anchor.Y + y, anchor.Z + z});
            }
        }
    }
    return positions;
}

std::size_t EstimateSurface(
    const SmartBrushShape shape,
    const int size) noexcept
{
    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    if (shape == SmartBrushShape::Cube)
        return static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    std::size_t count = 0U;
    const int radiusSquared = size * size;
    const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
    for (int second = minimumOffset; second <= maximumOffset; ++second)
        for (int first = minimumOffset; first <= maximumOffset; ++first)
        {
            const int dx = 2 * first - evenCenterOffset;
            const int dy = 2 * second - evenCenterOffset;
            if (dx * dx + dy * dy <= radiusSquared) ++count;
        }
    return count;
}

std::size_t EstimateVolume(
    const SmartBrushShape shape, const int size) noexcept
{
    if (shape == SmartBrushShape::Cube)
    {
        const std::size_t dimension = static_cast<std::size_t>(size);
        return dimension * dimension * dimension;
    }

    const int minimumOffset = -((size - 1) / 2);
    const int maximumOffset = size / 2;
    const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
    const int radiusSquared = size * size;
    std::size_t count = 0U;
    for (int z = minimumOffset; z <= maximumOffset; ++z)
    {
        for (int y = minimumOffset; y <= maximumOffset; ++y)
        {
            for (int x = minimumOffset; x <= maximumOffset; ++x)
            {
                const int dx = 2 * x - evenCenterOffset;
                const int dz = 2 * z - evenCenterOffset;
                if (shape == SmartBrushShape::Cylinder)
                {
                    if (dx * dx + dz * dz <= radiusSquared) ++count;
                    continue;
                }
                const int dy = 2 * y - evenCenterOffset;
                if (dx * dx + dy * dy + dz * dz <= radiusSquared) ++count;
            }
        }
    }
    return count;
}
}

int SmartBrushEngine::MaximumSize() noexcept
{
    return MaximumVoxelBrushSize;
}

std::size_t SmartBrushEngine::EstimateTotal(
    const SmartBrushState& state) noexcept
{
    if (!IsSupportedShape(state.Shape) || state.Size < 1 ||
        state.Size > MaximumSmartBrushRequestSize ||
        !IsKnownDimension(state.Dimension) ||
        !IsKnownOrientation(state.Orientation) ||
        !IsKnownMode(state.Mode) ||
        state.PreviewMode != SmartBrushPreviewMode::Adaptive)
    {
        return 0U;
    }
    if (state.Shape == SmartBrushShape::Cylinder &&
        state.Dimension != SmartBrushDimension::Volume3D)
        return 0U;
    if (state.Dimension == SmartBrushDimension::Surface2D)
        return EstimateSurface(state.Shape, state.Size);
    return EstimateVolume(state.Shape, state.Size);
}

bool SmartBrushResult::IsWithinBounds() const noexcept
{
    return Code == SmartBrushResultCode::Valid;
}

bool SmartBrushResult::HasAddablePositions() const noexcept
{
    return IsWithinBounds() && !AddablePositions.empty();
}

SmartBrushResult SmartBrushEngine::Resolve(const SmartBrushRequest& request)
{
    SmartBrushResult result;
    if (!IsSupportedShape(request.State.Shape))
    {
        result.Code = SmartBrushResultCode::Unsupported;
        result.Error = "The selected Smart Brush shape is not implemented.";
        return result;
    }
    if (request.State.Shape == SmartBrushShape::Cylinder &&
        request.State.Dimension != SmartBrushDimension::Volume3D)
    {
        result.Code = SmartBrushResultCode::Unsupported;
        result.Error = "Cylinder Brush supports only a vertical 3D volume.";
        return result;
    }
    if (request.MaximumSize < 1 ||
        request.MaximumSize > MaximumSmartBrushRequestSize ||
        request.State.Size < 1 ||
        request.State.Size > request.MaximumSize ||
        ((request.State.Mode == SmartBrushMode::Add ||
          request.State.Mode == SmartBrushMode::Paint) &&
         (request.State.PaletteIndex == 0U || request.State.PaletteIndex > 255U)) ||
        !IsKnownMode(request.State.Mode) ||
        !IsKnownDimension(request.State.Dimension) ||
        !IsKnownOrientation(request.State.Orientation) ||
        request.State.PreviewMode != SmartBrushPreviewMode::Adaptive ||
        !request.IsOccupied)
    {
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "The Smart Brush request is invalid.";
        return result;
    }
    try
    {
        if (request.State.Dimension == SmartBrushDimension::Volume3D)
        {
            if (request.State.Shape == SmartBrushShape::Cylinder)
            {
                result.Positions = GenerateVerticalCylinderVolume(
                    request.Placement, request.State.Size);
            }
            else
            {
                const VoxelBrushShape shape = request.State.Shape ==
                        SmartBrushShape::Cube
                    ? VoxelBrushShape::Cube : VoxelBrushShape::Sphere;
                result.Positions = request.State.Size > MaximumVoxelBrushSize
                    ? request.State.Shape == SmartBrushShape::Cube
                        ? GenerateCubeVolume(request.Placement, request.State.Size)
                        : GenerateSphereVolume(request.Placement, request.State.Size)
                    : GenerateVoxelBrush(
                        OffsetVoxelBrushAnchor(
                            request.Placement.Target,
                            request.Placement.Normal,
                            request.State.Size),
                        shape,
                        request.State.Size);
            }
        }
        else
        {
            result.Positions = GenerateSurface(
                request.Placement,
                request.State.Shape,
                ResolveOrientation(
                    request.State.Orientation, request.Placement.Normal),
                request.State.Size);
        }
        if (result.Positions.empty())
        {
            result.Code = SmartBrushResultCode::TechnicalFailure;
            result.Error = "The Smart Brush generated no positions.";
            return result;
        }
        result.Statistics.Total = result.Positions.size();
        std::vector<Asset::Voxel::VoxelPosition> rawPositions =
            std::move(result.Positions);
        const SmartBrushBounds rawBounds = CalculateBounds(rawPositions);
        result.Positions.reserve(rawPositions.size());
        result.ClippedPositions.reserve(rawPositions.size());
        for (const Asset::Voxel::VoxelPosition position : rawPositions)
        {
            if (!IsInside(position, request.Dimensions))
            {
                ++result.Statistics.Clipped;
                result.ClippedPositions.push_back(position);
                continue;
            }
            result.Positions.push_back(position);
        }
        if (result.Positions.empty())
        {
            result.Code = SmartBrushResultCode::OutOfBounds;
            return result;
        }
        result.RenderPlan.Bounds = CalculateBounds(result.Positions);
        // Erase previews show only occupied cells, so an aggregate outline
        // would claim empty cells are removable. Keep the render plan detailed
        // to make the preview exactly match the transaction.
        result.RenderPlan.Mode = request.State.Mode == SmartBrushMode::Erase
            ? SmartBrushRenderMode::DetailedCells
            : request.State.Shape == SmartBrushShape::Sphere &&
                result.Statistics.Total > MaximumDetailedBrushPreviewVoxelCount
            ? SmartBrushRenderMode::AggregateSphere
            : result.Positions.size() <= MaximumDetailedBrushPreviewVoxelCount
            ? SmartBrushRenderMode::DetailedCells
            : SmartBrushRenderMode::AggregateBox;
        if (result.RenderPlan.Mode == SmartBrushRenderMode::AggregateSphere)
        {
            const int centerOffset = (request.State.Size - 1) / 2;
            result.RenderPlan.SphereCenter = {
                rawBounds.Minimum.X + centerOffset,
                rawBounds.Minimum.Y + centerOffset,
                rawBounds.Minimum.Z + centerOffset};
            result.RenderPlan.SphereRadius = static_cast<std::uint32_t>(
                request.State.Size / 2);
        }
        result.AddablePositions.reserve(result.Positions.size());
        result.ExistingPositions.reserve(result.Positions.size());
        for (const Asset::Voxel::VoxelPosition position : result.Positions)
        {
            if (request.IsOccupied(position))
                result.ExistingPositions.push_back(position);
            else
                result.AddablePositions.push_back(position);
        }
        result.Statistics.New = result.AddablePositions.size();
        result.Statistics.Existing = result.ExistingPositions.size();
        result.Code = SmartBrushResultCode::Valid;
        return result;
    }
    catch (const std::exception& exception)
    {
        result.Code = SmartBrushResultCode::TechnicalFailure;
        result.Error = exception.what();
    }
    catch (...)
    {
        result.Code = SmartBrushResultCode::TechnicalFailure;
        result.Error = "Smart Brush resolution failed.";
    }
    return result;
}

} // namespace VoxelForge::Editor
