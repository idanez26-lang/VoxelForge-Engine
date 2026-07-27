#pragma once

// Compatibility adapter retained for older callers. It delegates Paint
// planning and rendering to the immutable SmartToolPlan pipeline.
#include "SmartTools/SmartBrushPreviewResolver.h"

#include <optional>

namespace VoxelForge::Editor
{
struct LegacySmartBrushPreviewRequest final
{
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    SmartBrushState State{};
    SmartBrushPlacement Placement{};
    std::array<float, 4> ActivePaletteColor{1.0F, 1.0F, 1.0F, 1.0F};
    float Alpha = GhostPreviewStyle::DefaultAlpha;
};

struct LegacySmartBrushPreviewCacheKey final
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

    [[nodiscard]] bool operator==(const LegacySmartBrushPreviewCacheKey& other)
        const noexcept;
};

class LegacySmartBrushPreviewResolver final
{
public:
    [[nodiscard]] static SmartBrushPreviewResult Resolve(
        const LegacySmartBrushPreviewRequest& request);
};

class LegacySmartBrushPreviewCache final
{
public:
    [[nodiscard]] const SmartBrushPreviewResult& Resolve(
        const LegacySmartBrushPreviewCacheKey& key,
        const LegacySmartBrushPreviewRequest& request);
    void Clear() noexcept;
    [[nodiscard]] std::size_t ResolutionCount() const noexcept;

private:
    std::optional<LegacySmartBrushPreviewCacheKey> key_;
    SmartBrushPreviewResult result_;
    std::size_t resolutionCount_ = 0U;
};
} // namespace VoxelForge::Editor
