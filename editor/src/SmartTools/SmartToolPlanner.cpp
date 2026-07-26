#include "SmartTools/SmartToolPlanner.h"

#include "SmartTools/SmartToolPlan.h"

#include <cstdint>
#include <string>
#include <unordered_set>
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

using PositionSet = std::unordered_set<Asset::Voxel::VoxelPosition, PositionHash>;

[[nodiscard]] std::vector<SmartToolPlanCell> BuildCells(
    const SmartBrushResult& brush, const SmartBrushState& state)
{
    const PositionSet addable(
        brush.AddablePositions.begin(), brush.AddablePositions.end());
    const PositionSet existing(
        brush.ExistingPositions.begin(), brush.ExistingPositions.end());
    std::vector<SmartToolPlanCell> cells;
    cells.reserve(brush.Positions.size() + brush.ClippedPositions.size());
    for (const Asset::Voxel::VoxelPosition position : brush.Positions)
    {
        const bool erase = state.Mode == SmartBrushMode::Erase &&
            existing.contains(position);
        const bool add = state.Mode == SmartBrushMode::Add &&
            addable.contains(position);
        cells.push_back({position,
            add ? SmartToolCellOperation::Add
                : erase ? SmartToolCellOperation::Erase
                        : SmartToolCellOperation::Ignore,
            state.PaletteIndex,
            add ? SmartToolPlanPreviewState::Added
                : erase ? SmartToolPlanPreviewState::Erased
                        : SmartToolPlanPreviewState::Ignored});
    }
    for (const Asset::Voxel::VoxelPosition position : brush.ClippedPositions)
        cells.push_back({position, SmartToolCellOperation::Ignore,
            state.PaletteIndex, SmartToolPlanPreviewState::Clipped});
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
            "SMART-01 only supports Pencil geometry.");
    }
    if (request.Action != SmartAction::Add && request.Action != SmartAction::Erase)
    {
        return Error(SmartBrushResultCode::Unsupported,
            "SMART-01 only supports Add and Erase actions.");
    }

    SmartToolRequest normalized = request;
    normalized.BrushRequest.State.Shape = ResolveSmartBrushShape(
        request.Geometry, normalized.BrushRequest.State.Shape);
    normalized.BrushRequest.State.Mode = request.Action == SmartAction::Erase
        ? SmartBrushMode::Erase : SmartBrushMode::Add;
    SmartBrushResult brush = SmartBrushEngine::Resolve(normalized.BrushRequest);
    const SmartBrushResultCode code = brush.Code;
    const std::string error = brush.Error;
    std::vector<SmartToolDiagnostic> diagnostics;
    if (!error.empty()) diagnostics.push_back({SmartToolStatusFrom(code), error});
    std::vector<SmartToolPlanCell> cells = BuildCells(
        brush, normalized.BrushRequest.State);
    const std::uint64_t planId = nextPlanId_++;
    const SmartToolPlanPtr plan(new SmartToolPlan(normalized, std::move(brush),
        planId, planId, std::move(cells), std::move(diagnostics)));
    return {SmartToolStatusFrom(code), code, plan, error};
}
} // namespace VoxelForge::Editor
