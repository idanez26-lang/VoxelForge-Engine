#pragma once

#include "VoxModel.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoxelForge::Asset::Vox
{

inline constexpr std::uintmax_t MaximumVoxFileSize = 64U * 1024U * 1024U;
inline constexpr std::uint32_t MaximumVoxDimension = 2048U;
inline constexpr std::uint32_t MaximumVoxModelCount = 4096U;
inline constexpr std::uint64_t MaximumVoxVoxelCount = 16U * 1024U * 1024U;
inline constexpr std::uint32_t MaximumVoxChunkCount = 100000U;
inline constexpr std::uint32_t MaximumVoxChunkDepth = 64U;
inline constexpr std::size_t VoxPaletteSize = 256U;

[[nodiscard]] const std::array<VoxColor, VoxPaletteSize>&
DefaultVoxPalette() noexcept;

} // namespace VoxelForge::Asset::Vox
