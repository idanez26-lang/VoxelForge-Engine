#pragma once

#include "BrushEngine/SmartBrushEngine.h"
#include "SmartTools/SmartToolPlan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

struct SmartBrushPreviewRequest final
{
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    SmartBrushState State{};
    SmartBrushPlacement Placement{};
    std::array<float, 4> ActivePaletteColor{1.0F, 1.0F, 1.0F, 1.0F};
    float Alpha = GhostPreviewStyle::DefaultAlpha;
};

// Kept independent from the workspace so cache behaviour can be verified
// without booting the editor. A document generation protects pointer reuse;
// its revision invalidates the result after every voxel edit.
struct SmartBrushPreviewCacheKey final
{
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::uint64_t DocumentGeneration = 0U;
    SmartBrushState State{};
    std::uint8_t GeometryKey = 0U;
    SmartBrushPlacement Placement{};
    std::array<float, 4> ActivePaletteColor{};
    float Alpha = GhostPreviewStyle::DefaultAlpha;

    [[nodiscard]] bool operator==(const SmartBrushPreviewCacheKey& other)
        const noexcept
    {
        return Document == other.Document && SubModelIndex == other.SubModelIndex &&
            DocumentRevision == other.DocumentRevision &&
            DocumentGeneration == other.DocumentGeneration && State == other.State &&
            GeometryKey == other.GeometryKey &&
            Placement.Target == other.Placement.Target &&
            Placement.Normal == other.Placement.Normal &&
            ActivePaletteColor == other.ActivePaletteColor && Alpha == other.Alpha;
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
        const SmartBrushPreviewRequest& request);
    [[nodiscard]] static SmartBrushPreviewResult Resolve(
        const SmartToolPlan& plan,
        const std::array<float, 4>& activePaletteColor,
        float alpha = GhostPreviewStyle::DefaultAlpha);
};

class SmartBrushPreviewCache final
{
public:
    [[nodiscard]] const SmartBrushPreviewResult& Resolve(
        const SmartBrushPreviewCacheKey& key, const SmartBrushPreviewRequest& request);
    void Clear() noexcept;
    [[nodiscard]] std::size_t ResolutionCount() const noexcept;

private:
    std::optional<SmartBrushPreviewCacheKey> key_;
    SmartBrushPreviewResult result_;
    std::size_t resolutionCount_ = 0U;
};
} // namespace VoxelForge::Editor
