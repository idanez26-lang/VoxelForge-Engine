#pragma once

#include "SmartTools/SmartPreviewTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace VoxelForge::Editor
{
enum class FacePlanGhostSide : std::uint8_t
{
    NegativeX = 0U,
    PositiveX,
    NegativeY,
    PositiveY,
    NegativeZ,
    PositiveZ
};

struct FacePlanGhostSurfaceCell final
{
    std::size_t GhostIndex = 0U;
    std::uint8_t ExposedFaceMask = 0U;
};

struct FacePlanGhostSurface final
{
    std::vector<FacePlanGhostSurfaceCell> Cells;
    std::size_t ExposedFaceCount = 0U;
};

struct FacePlanGhostVoxelBounds final
{
    std::array<float, 3U> Minimum{};
    std::array<float, 3U> Maximum{};
};

inline constexpr float FacePlanGhostOutwardOffset = 0.003F;

[[nodiscard]] constexpr std::uint8_t FacePlanGhostSideBit(
    const FacePlanGhostSide side) noexcept
{
    return static_cast<std::uint8_t>(
        1U << static_cast<std::uint8_t>(side));
}

[[nodiscard]] constexpr FacePlanGhostVoxelBounds
MakeFacePlanGhostVoxelBounds(
    const Asset::Voxel::VoxelPosition position) noexcept
{
    return {{
                static_cast<float>(position.X),
                static_cast<float>(position.Y),
                static_cast<float>(position.Z)},
        {
            static_cast<float>(position.X) + 1.0F,
            static_cast<float>(position.Y) + 1.0F,
            static_cast<float>(position.Z) + 1.0F}};
}

[[nodiscard]] constexpr std::array<float, 3U>
FacePlanGhostSideNormal(const FacePlanGhostSide side) noexcept
{
    switch (side)
    {
    case FacePlanGhostSide::NegativeX: return {-1.0F, 0.0F, 0.0F};
    case FacePlanGhostSide::PositiveX: return {1.0F, 0.0F, 0.0F};
    case FacePlanGhostSide::NegativeY: return {0.0F, -1.0F, 0.0F};
    case FacePlanGhostSide::PositiveY: return {0.0F, 1.0F, 0.0F};
    case FacePlanGhostSide::NegativeZ: return {0.0F, 0.0F, -1.0F};
    case FacePlanGhostSide::PositiveZ: return {0.0F, 0.0F, 1.0F};
    }
    return {};
}

[[nodiscard]] constexpr std::array<float, 3U>
OffsetFacePlanGhostPointOutward(
    const std::array<float, 3U> point,
    const FacePlanGhostSide side,
    const float offset = FacePlanGhostOutwardOffset) noexcept
{
    const std::array<float, 3U> normal = FacePlanGhostSideNormal(side);
    return {
        point[0] + normal[0] * offset,
        point[1] + normal[1] * offset,
        point[2] + normal[2] * offset};
}

// Produces the exact exposed envelope of the materialized Face plan. The
// first ghost at a duplicate coordinate remains authoritative, matching the
// renderer's stable input order. Construction and neighbor queries are O(N).
[[nodiscard]] inline FacePlanGhostSurface BuildFacePlanGhostSurface(
    const std::span<const GhostVoxel> ghosts)
{
    struct PositionHash final
    {
        [[nodiscard]] std::size_t operator()(
            const Asset::Voxel::VoxelPosition position) const noexcept
        {
            const auto mix = [](std::size_t value) noexcept
            {
                value ^= value >> 16U;
                value *= static_cast<std::size_t>(0x7feb352dU);
                value ^= value >> 15U;
                value *= static_cast<std::size_t>(0x846ca68bU);
                return value ^ (value >> 16U);
            };
            return mix(static_cast<std::uint32_t>(position.X)) ^
                (mix(static_cast<std::uint32_t>(position.Y)) << 1U) ^
                (mix(static_cast<std::uint32_t>(position.Z)) << 2U);
        }
    };

    using Position = Asset::Voxel::VoxelPosition;
    std::unordered_map<Position, std::size_t, PositionHash> positions;
    positions.reserve(ghosts.size());
    for (std::size_t index = 0U; index < ghosts.size(); ++index)
        positions.try_emplace(ghosts[index].Position, index);

    constexpr std::array<Position, 6U> Neighbors{{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
    FacePlanGhostSurface result;
    result.Cells.reserve(positions.size());
    for (std::size_t index = 0U; index < ghosts.size(); ++index)
    {
        const Position position = ghosts[index].Position;
        const auto authoritative = positions.find(position);
        if (authoritative == positions.end() ||
            authoritative->second != index)
            continue;

        std::uint8_t mask = 0U;
        for (std::size_t side = 0U; side < Neighbors.size(); ++side)
        {
            const Position neighbor{
                position.X + Neighbors[side].X,
                position.Y + Neighbors[side].Y,
                position.Z + Neighbors[side].Z};
            if (!positions.contains(neighbor))
            {
                mask = static_cast<std::uint8_t>(mask |
                    FacePlanGhostSideBit(
                        static_cast<FacePlanGhostSide>(side)));
                ++result.ExposedFaceCount;
            }
        }
        if (mask != 0U) result.Cells.push_back({index, mask});
    }
    return result;
}
} // namespace VoxelForge::Editor
