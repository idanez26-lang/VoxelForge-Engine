#pragma once

#include <array>
#include <cstdint>

namespace VoxelForge::Voxel::VoxelFileFormat
{

// VFVOXEL v1 is a packed byte stream with no implicit alignment:
// header, MODL(name), PLTE(256 RGBA8 colors), GRDS, then GRID records.
// Every integer is encoded little-endian and every string is length-prefixed
// UTF-8. Each voxel is exactly ColorIndex followed by Flags (two bytes).
inline constexpr std::array<std::uint8_t, 8> Signature{
    'V', 'F', 'V', 'O', 'X', 'E', 'L', 0};
inline constexpr std::uint16_t MajorVersion = 1;
inline constexpr std::uint16_t MinorVersion = 0;
inline constexpr std::uint32_t FormatFlags = 0;

[[nodiscard]] consteval std::uint32_t MakeSectionIdentifier(
    const char first,
    const char second,
    const char third,
    const char fourth) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(first)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(second)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(third)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(fourth)) << 24U);
}

inline constexpr std::uint32_t ModelSection =
    MakeSectionIdentifier('M', 'O', 'D', 'L');
inline constexpr std::uint32_t PaletteSection =
    MakeSectionIdentifier('P', 'L', 'T', 'E');
inline constexpr std::uint32_t GridsSection =
    MakeSectionIdentifier('G', 'R', 'D', 'S');
inline constexpr std::uint32_t GridSection =
    MakeSectionIdentifier('G', 'R', 'I', 'D');

inline constexpr std::uint32_t PaletteColorCount = 256;
inline constexpr std::uint32_t MaximumNameByteCount = 4096;
inline constexpr std::uint32_t MaximumGridCount = 32;
inline constexpr std::uint32_t MaximumGridDimension = 256;
inline constexpr std::uint64_t MaximumTotalVoxelCount =
    64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t MaximumFileByteCount =
    256ULL * 1024ULL * 1024ULL;

// All flag bits are intentionally serialized verbatim. V1 only interprets the
// occupancy bit; reserved bits survive a round-trip for forward compatibility.

} // namespace VoxelForge::Voxel::VoxelFileFormat
