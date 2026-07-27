#include "SmartTools/SmartToolPlanner.h"

#include "SmartTools/SmartToolPlan.h"

#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>
#include <utility>

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
        request.Geometry != SmartGeometry::Sphere)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "The Smart Tool planner supports only Pencil, Cube, and Sphere geometry.");
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
        normalized.BrushRequest.State.Shape = ResolveSmartBrushShape(
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

        SmartBrushResult brush = SmartBrushEngine::Resolve(geometryRequest);
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
