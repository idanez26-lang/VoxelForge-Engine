#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

// V1 is an explicitly little-endian, directory-based container.  Its fixed
// header is 32 bytes: magic[8], major u16, minor u16, headerFlags u32,
// chunkCount u32, directoryOffset u32, directorySize u32, reserved u32.
// Each 32-byte directory entry is: fourCC u32, flags u32, payloadOffset u64,
// payloadSize u64, payloadCrc32 u32, reserved u32.  Payloads may be placed in
// any non-overlapping order after the header and directory.
inline constexpr std::array<std::byte, 8U> VfstampMagic{
    static_cast<std::byte>('V'), static_cast<std::byte>('F'), static_cast<std::byte>('S'),
    static_cast<std::byte>('T'), static_cast<std::byte>('A'), static_cast<std::byte>('M'),
    static_cast<std::byte>('P'), std::byte{0U}};
inline constexpr std::uint16_t VfstampFormatMajorVersion = 1U;
inline constexpr std::uint16_t VfstampFormatMinorVersion = 0U;
inline constexpr std::size_t VfstampHeaderSize = 32U;
inline constexpr std::size_t VfstampDirectoryEntrySize = 32U;
inline constexpr std::size_t VfstampHeaderMagicOffset = 0U;
inline constexpr std::size_t VfstampHeaderMajorVersionOffset = 8U;
inline constexpr std::size_t VfstampHeaderMinorVersionOffset = 10U;
inline constexpr std::size_t VfstampHeaderFlagsOffset = 12U;
inline constexpr std::size_t VfstampHeaderChunkCountOffset = 16U;
inline constexpr std::size_t VfstampHeaderDirectoryOffset = 20U;
inline constexpr std::size_t VfstampHeaderDirectorySizeOffset = 24U;
inline constexpr std::size_t VfstampHeaderReservedOffset = 28U;
inline constexpr std::size_t VfstampDirectoryEntryIdOffset = 0U;
inline constexpr std::size_t VfstampDirectoryEntryFlagsOffset = 4U;
inline constexpr std::size_t VfstampDirectoryEntryPayloadOffset = 8U;
inline constexpr std::size_t VfstampDirectoryEntryPayloadSizeOffset = 16U;
inline constexpr std::size_t VfstampDirectoryEntryCrc32Offset = 24U;
inline constexpr std::size_t VfstampDirectoryEntryReservedOffset = 28U;

inline constexpr std::uint32_t MakeVfstampChunkId(
    const char first,
    const char second,
    const char third,
    const char fourth) noexcept
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(first)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(fourth)) << 24U);
}

inline constexpr std::uint32_t VfstampChunkManf = MakeVfstampChunkId('M', 'A', 'N', 'F');
inline constexpr std::uint32_t VfstampChunkPal0 = MakeVfstampChunkId('P', 'A', 'L', '0');
inline constexpr std::uint32_t VfstampChunkVox0 = MakeVfstampChunkId('V', 'O', 'X', '0');
inline constexpr std::uint32_t VfstampChunkHash = MakeVfstampChunkId('H', 'A', 'S', 'H');
inline constexpr std::uint32_t VfstampChunkThmb = MakeVfstampChunkId('T', 'H', 'M', 'B');
inline constexpr std::uint32_t VfstampChunkAnch = MakeVfstampChunkId('A', 'N', 'C', 'H');
inline constexpr std::uint32_t VfstampChunkExtn = MakeVfstampChunkId('E', 'X', 'T', 'N');

inline constexpr std::uint32_t VfstampChunkFlagRequired = 1U << 0U;
inline constexpr std::uint32_t VfstampKnownChunkFlags = VfstampChunkFlagRequired;
inline constexpr std::uint32_t VfstampKnownHeaderFlags = 0U;
inline constexpr std::size_t VfstampStructuralHashSize = 8U;
// STAMP-03 V1 writers emit a HASH payload containing the structural hash
// followed by the logical domain hash.  The reader also accepts the original
// eight-byte structural-only form so STAMP-02 container fixtures remain valid.
inline constexpr std::size_t VfstampHashPayloadSize = 16U;

struct VfstampChunk final
{
    std::uint32_t Id = 0U;
    std::uint32_t Flags = 0U;
    std::uint32_t Crc32 = 0U;
    std::vector<std::byte> Payload;

    [[nodiscard]] bool operator==(const VfstampChunk&) const noexcept = default;
};

struct VfstampContainer final
{
    std::uint16_t MajorVersion = VfstampFormatMajorVersion;
    std::uint16_t MinorVersion = VfstampFormatMinorVersion;
    std::uint32_t HeaderFlags = 0U;
    std::vector<VfstampChunk> Chunks;

    [[nodiscard]] bool operator==(const VfstampContainer&) const noexcept = default;
};

// IEEE CRC-32, initial value 0xffffffff and final xor 0xffffffff.
[[nodiscard]] std::uint32_t ComputeVfstampCrc32(std::span<const std::byte> bytes) noexcept;

// FNV-1a 64-bit over the V1 structural records.  The HASH entry is excluded
// because its payload contains this value.  Each remaining record contributes
// Id, Flags, Offset, Size and CRC32 in little-endian byte order, preceded by
// the major version, minor version and count of non-HASH records.  Per-chunk
// CRCs bind the hash to payload contents without a circular dependency.
[[nodiscard]] std::uint64_t ComputeVfstampStructuralHash(
    std::uint16_t majorVersion,
    std::uint16_t minorVersion,
    std::span<const std::uint32_t> ids,
    std::span<const std::uint32_t> flags,
    std::span<const std::uint64_t> offsets,
    std::span<const std::uint64_t> sizes,
    std::span<const std::uint32_t> crcs) noexcept;

} // namespace VoxelForge::Editor::Stamps
