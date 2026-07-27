#pragma once

#include "SmartTools/SmartToolExecutionContext.h"
#include "SmartTools/SmartToolPlan.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <span>
#include <string>

namespace VoxelForge::Editor
{

class VoxelEditHistory;

enum class VoxelToolResultCode
{
    Applied,
    NoChange,
    NoDocument,
    NoHit,
    TargetOutOfBounds,
    TargetOccupied,
    TargetEmpty,
    InvalidModel,
    InvalidPaletteIndex,
    Blocked,
    Failed
};

struct VoxelToolResult final
{
    VoxelToolResultCode Code = VoxelToolResultCode::Failed;
    bool Changed = false;
    Asset::Voxel::VoxelPosition Position{};
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

struct VoxelPencilContext final
{
    // The exact immutable plan rendered by the preview. Pencil deliberately
    // receives no hit, brush state, or request from which to recalculate one.
    SmartToolPlanPtr Plan;
    SmartToolExecutionContext Execution;
};

class VoxelPencilTool final
{
public:
    // Commits an existing plan only; there is intentionally no BrushEngine
    // fallback for a missing or stale plan.
    [[nodiscard]] static VoxelToolResult Apply(
        const VoxelPencilContext& context);
    // Commits one already de-duplicated set of planner-produced changes. This
    // is the only continuous-stroke write boundary and still creates one
    // VoxelEditOperation, never one operation per sampled cell.
    [[nodiscard]] static VoxelToolResult ApplyChanges(
        const SmartToolExecutionContext& execution, SmartAction action,
        Asset::Voxel::VoxelPosition target,
        std::span<const VoxelChange> changes);
};

[[nodiscard]] const char* VoxelToolResultCodeName(
    VoxelToolResultCode code) noexcept;

} // namespace VoxelForge::Editor
