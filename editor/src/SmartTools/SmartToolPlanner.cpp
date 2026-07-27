#include "SmartTools/SmartToolPlanner.h"

#include "SmartTools/SmartToolPlan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
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
        request.Geometry != SmartGeometry::Face)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "The Smart Tool planner supports only Pencil, Cube, Sphere, and Face geometry.");
    }
    if (request.Mode && request.Geometry != SmartGeometry::Pencil)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "SMART-05 modes require the Pencil Smart Geometry.");
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
            normalized.Geometry = SmartGeometry::Pencil;
            normalized.BrushRequest.State.Dimension =
                SmartBrushDimension::Volume3D;
            normalized.BrushRequest.State.Orientation = SmartBrushOrientation::Auto;
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

        SmartBrushResult brush = request.Geometry == SmartGeometry::Face
            ? ResolveFace(normalized, snapshot)
            : SmartBrushEngine::Resolve(geometryRequest);
        if (request.Geometry == SmartGeometry::Face &&
            brush.Code == SmartBrushResultCode::InvalidRequest)
            return Error(brush.Code, std::move(brush.Error));
        const SmartBrushResultCode code = brush.Code;
        const std::string error = brush.Error;
        std::vector<SmartToolDiagnostic> diagnostics;
        if (!error.empty())
            diagnostics.push_back({SmartToolStatusFrom(code), error});
        std::vector<SmartToolPlanCell> cells =
            BuildCells(brush, normalized, snapshot);
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
