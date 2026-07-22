#pragma once

#include "BrushEngine/SmartBrushEngine.h"
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

struct VoxelPlacementPreview final
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

[[nodiscard]] VoxelPlacementPreview EvaluateVoxelPencilPreview(
    const Asset::Voxel::VoxelDocument* document,
    std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    bool pencilActive,
    std::optional<Asset::Voxel::VoxelPosition> workplaneTarget =
        std::nullopt,
    SmartBrushState state = {}) noexcept;

[[nodiscard]] VoxelPlacementPreview EvaluateVoxelPencilPreview(
    const Asset::Voxel::VoxelDocument* document,
    std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    bool pencilActive,
    std::optional<Asset::Voxel::VoxelPosition> workplaneTarget,
    VoxelBrushShape brush,
    int brushSize) noexcept;

[[nodiscard]] VoxelPlacementPreview EvaluateVoxelEraserPreview(
    const Asset::Voxel::VoxelDocument* document,
    std::size_t subModelIndex,
    const std::optional<VoxelRaycastHit>& hit,
    bool eraserActive,
    bool blocked = false) noexcept;

[[nodiscard]] const char* VoxelPlacementPreviewStatusName(
    VoxelPlacementPreviewStatus status) noexcept;

} // namespace VoxelForge::Editor
