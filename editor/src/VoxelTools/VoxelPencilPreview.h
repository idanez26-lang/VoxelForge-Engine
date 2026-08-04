#pragma once

#include "SmartTools/SmartToolPlan.h"
#include "VoxelSelection/VoxelRaycast.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace VoxelForge::Editor
{

enum class VoxelPlacementPreviewStatus
{
    Unavailable,
    Valid,
    OutOfBounds,
    Occupied,
    TargetMissing,
    Blocked
};

enum class VoxelPreviewTool
{
    None,
    Pencil,
    Eraser
};

struct VoxelToolPlacementPreview final
{
    VoxelPlacementPreviewStatus Status =
        VoxelPlacementPreviewStatus::Unavailable;
    std::optional<Asset::Voxel::VoxelPosition> Position;
    VoxelPreviewTool Tool = VoxelPreviewTool::None;
    std::vector<Asset::Voxel::VoxelPosition> Positions;
    std::vector<Asset::Voxel::VoxelPosition> AddablePositions;
    std::vector<Asset::Voxel::VoxelPosition> OccupiedPositions;
    SmartBrushStatistics Statistics{};
    SmartBrushRenderPlan RenderPlan{};

    [[nodiscard]] bool IsVisible() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
};

[[nodiscard]] VoxelToolPlacementPreview EvaluateVoxelEraserPreview(
    const Asset::Voxel::VoxelDocument* document,
    std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    bool eraserActive,
    bool blocked = false) noexcept;

[[nodiscard]] const char* VoxelPlacementPreviewStatusName(
    VoxelPlacementPreviewStatus status) noexcept;

} // namespace VoxelForge::Editor
