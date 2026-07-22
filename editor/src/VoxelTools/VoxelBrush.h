#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VoxelForge::Editor
{

// For an even size, the generated anchor is the lower of the two central
// voxels: size 2 covers offsets [0, +1], size 4 covers [-1, +2].
enum class VoxelBrushShape : std::uint8_t
{
    Cube,
    Sphere
};

inline constexpr int MaximumVoxelBrushSize = 16;
// Detailed wireframes use twelve edge boxes per displayed voxel. Larger
// brushes use one shape-aware aggregate outline while retaining their full
// plan for preview validation and final atomic application.
inline constexpr std::size_t MaximumDetailedBrushPreviewVoxelCount = 256U;

enum class VoxelBrushPreviewRenderMode : std::uint8_t
{
    DetailedCells,
    AggregateBox,
    AggregateSphere
};

[[nodiscard]] bool UsesAggregateBrushPreview(
    std::size_t voxelCount) noexcept;
[[nodiscard]] VoxelBrushPreviewRenderMode SelectVoxelBrushPreviewRenderMode(
    VoxelBrushShape shape,
    std::size_t voxelCount) noexcept;

[[nodiscard]] bool IsVoxelBrushSizeValid(int size) noexcept;

// Keeps a centred brush tangent to the placement surface while moving it
// forward along that surface's placement normal. Size 3 therefore starts at
// the workplane / adjacent voxel instead of extending one voxel behind it.
[[nodiscard]] Asset::Voxel::VoxelPosition OffsetVoxelBrushAnchor(
    Asset::Voxel::VoxelPosition placementTarget,
    Asset::Voxel::VoxelPosition placementNormal,
    int size) noexcept;

// Returns unique, deterministic positions without reading or mutating a
// document. Callers use this shared plan for both preview and application.
[[nodiscard]] std::vector<Asset::Voxel::VoxelPosition> GenerateVoxelBrush(
    Asset::Voxel::VoxelPosition anchor,
    VoxelBrushShape shape,
    int size);

[[nodiscard]] std::size_t EstimateVoxelBrushVoxelCount(
    VoxelBrushShape shape,
    int size);

} // namespace VoxelForge::Editor
