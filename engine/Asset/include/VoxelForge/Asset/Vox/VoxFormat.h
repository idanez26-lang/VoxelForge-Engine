#pragma once

#include "VoxModel.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoxelForge::Asset::Vox
{

inline constexpr std::uintmax_t MaximumVoxFileSize = 64U * 1024U * 1024U;
inline constexpr std::uint32_t MaximumVoxDimension = 2048U;
// VF-STAB-01 bug 3 — the largest dimension the VOX format can WRITE back. XYZI
// stores voxel coordinates as uint8_t (VoxModel.h), so 256 values per axis is a
// hard ceiling of the format itself, not a policy we chose. Reading tolerates
// up to MaximumVoxDimension so third-party files still open, but a document
// above this ceiling can never be serialized again and is opened read-only.
inline constexpr std::uint32_t MaximumWritableVoxDimension = 256U;
inline constexpr std::uint32_t MaximumVoxModelCount = 4096U;
inline constexpr std::uint64_t MaximumVoxVoxelCount = 16U * 1024U * 1024U;
inline constexpr std::uint32_t MaximumVoxChunkCount = 100000U;
inline constexpr std::uint32_t MaximumVoxChunkDepth = 64U;
inline constexpr std::size_t VoxPaletteSize = 256U;

[[nodiscard]] const std::array<VoxColor, VoxPaletteSize>&
DefaultVoxPalette() noexcept;

} // namespace VoxelForge::Asset::Vox
