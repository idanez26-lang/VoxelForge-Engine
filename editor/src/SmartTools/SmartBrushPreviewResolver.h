#pragma once

#include "SmartTools/SmartToolPlan.h"
#include "SmartTools/SmartPreviewTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{
struct GhostPreviewStatistics final
{
    std::size_t Total = 0U;
    std::size_t Affected = 0U;
    std::size_t Ignored = 0U;
    std::size_t Clipped = 0U;

    [[nodiscard]] bool IsConsistent() const noexcept
    {
        return Total == Affected + Ignored + Clipped;
    }
};

struct SmartBrushPreviewResult final
{
    SmartBrushResultCode Code = SmartBrushResultCode::InvalidRequest;
    std::vector<GhostVoxel> GhostVoxels;
    std::vector<Asset::Voxel::VoxelPosition> AffectedPositions;
    GhostPreviewStatistics Statistics{};
    SmartBrushRenderPlan RenderPlan{};
    std::string Error;

    [[nodiscard]] bool IsAvailable() const noexcept
    {
        return Code == SmartBrushResultCode::Valid ||
            Code == SmartBrushResultCode::OutOfBounds;
    }
};

class SmartBrushPreviewResolver final
{
public:
    [[nodiscard]] static SmartBrushPreviewResult Resolve(
        const SmartToolPlan& plan,
        const std::array<float, 4>& activePaletteColor,
        float alpha = GhostPreviewStyle::DefaultAlpha);
};
} // namespace VoxelForge::Editor
