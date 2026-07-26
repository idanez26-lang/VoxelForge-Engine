#pragma once

#include "Commands/Voxel/VoxelEditSession.h"
#include "BrushEngine/SmartBrushEngine.h"
#include "SmartTools/SmartToolExecutionContext.h"
#include "SmartTools/SmartToolPlan.h"
#include "VoxelSelection/VoxelRaycast.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

class VoxelEditHistory;

enum class VoxelToolResultCode
{
    Applied,
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
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::optional<VoxelRaycastHit> Hit;
    SmartBrushState State{};
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
    std::optional<Asset::Voxel::VoxelPosition> WorkplaneTarget;
    // When supplied by SMART-01, this is the same immutable plan rendered by
    // the ghost preview. Kept last to preserve existing aggregate callers.
    SmartToolPlanPtr Plan;
    SmartToolExecutionContext Execution;
};

class VoxelPencilTool final
{
public:
    [[nodiscard]] static VoxelToolResult Apply(
        const VoxelPencilContext& context);
};

[[nodiscard]] const char* VoxelToolResultCodeName(
    VoxelToolResultCode code) noexcept;

} // namespace VoxelForge::Editor
