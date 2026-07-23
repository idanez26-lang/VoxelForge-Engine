#pragma once

#include "BrushEngine/SmartBrushEngine.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelSelection/VoxelRaycast.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

class VoxelEditHistory;

enum class VoxelPaintBrushResultCode : std::uint8_t
{
    Applied,
    NoChange,
    NoDocument,
    NoHit,
    TargetOutOfBounds,
    InvalidModel,
    InvalidPaletteIndex,
    Blocked,
    Failed
};

struct VoxelPaintBrushStatistics final
{
    std::size_t Total = 0U;
    std::size_t Painted = 0U;
    std::size_t Ignored = 0U;
    std::size_t Clipped = 0U;

    [[nodiscard]] bool IsConsistent() const noexcept
    {
        return Total == Painted + Ignored + Clipped;
    }
};

struct VoxelPaintBrushContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::optional<VoxelRaycastHit> Hit;
    SmartBrushState State{};
    bool Blocked = false;
    VoxelEditHistory* History = nullptr;
};

struct VoxelPaintBrushEvaluation final
{
    VoxelPaintBrushResultCode Code = VoxelPaintBrushResultCode::Failed;
    std::optional<Asset::Voxel::VoxelPosition> Target;
    VoxelPaintBrushStatistics Statistics{};
    std::vector<Asset::Voxel::VoxelPosition> Positions;
    std::vector<Asset::Voxel::VoxelPosition> PaintablePositions;
    std::vector<Asset::Voxel::VoxelPosition> IgnoredPositions;
    SmartBrushRenderPlan RenderPlan{};
    std::string Error;

    [[nodiscard]] bool IsResolved() const noexcept
    {
        return Code == VoxelPaintBrushResultCode::Applied ||
            Code == VoxelPaintBrushResultCode::NoChange;
    }
};

struct VoxelPaintBrushResult final
{
    VoxelPaintBrushResultCode Code = VoxelPaintBrushResultCode::Failed;
    bool Changed = false;
    Asset::Voxel::VoxelPosition Position{};
    VoxelPaintBrushStatistics Statistics{};
    std::uint64_t RevisionBefore = 0U;
    std::uint64_t RevisionAfter = 0U;
    std::string Error;
};

class VoxelPaintBrushTool final
{
public:
    // Resolves Paint geometry through SmartBrushEngine using a private Add-mode
    // copy. It never creates or deletes voxels: only occupied, different-color
    // cells are returned as paintable.
    [[nodiscard]] static VoxelPaintBrushEvaluation Evaluate(
        const VoxelPaintBrushContext& context);
    [[nodiscard]] static VoxelPaintBrushResult Apply(
        const VoxelPaintBrushContext& context);
};

[[nodiscard]] const char* VoxelPaintBrushResultCodeName(
    VoxelPaintBrushResultCode code) noexcept;

} // namespace VoxelForge::Editor
