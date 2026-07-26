#pragma once

#include "SmartTools/SmartToolPlan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{
enum class GhostVoxelState : std::uint8_t
{
    Added,
    Erased,
    Painted,
    Ignored,
    Invalid,
    Clipped
};

struct GhostVoxel final
{
    Asset::Voxel::VoxelPosition Position{};
    GhostVoxelState State = GhostVoxelState::Invalid;
    std::array<float, 4> Color{};
    float Alpha = 0.5F;
};

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

struct GhostPreviewStyle final
{
    static constexpr float DefaultAlpha = 0.5F;
    static constexpr std::array<float, 4> Added{0.25F, 0.92F, 0.43F, 1.0F};
    static constexpr std::array<float, 4> Erased{0.95F, 0.26F, 0.28F, 1.0F};
    static constexpr std::array<float, 4> Ignored{1.0F, 0.59F, 0.18F, 1.0F};
    static constexpr std::array<float, 4> Invalid{0.52F, 0.55F, 0.60F, 1.0F};
    static constexpr std::array<float, 4> Clipped{0.45F, 0.08F, 0.10F, 1.0F};
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
