#include "VoxelStamps/Format/VfstampReader.h"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

constexpr std::uint64_t VfstampVoxelRecordSize = 13U;

struct DirectoryEntry final
{
    std::uint32_t Id = 0U;
    std::uint32_t Flags = 0U;
    std::uint64_t Offset = 0U;
    std::uint64_t Size = 0U;
    std::uint32_t Crc32 = 0U;
    std::uint32_t Reserved = 0U;
};

[[nodiscard]] constexpr bool AddWouldOverflow(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    return right > std::numeric_limits<std::uint64_t>::max() - left;
}

[[nodiscard]] constexpr bool MultiplyWouldOverflow(
    const std::uint64_t left,
    const std::uint64_t right) noexcept
{
    return left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left;
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset])) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1U]))
            << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2U]))
            << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3U]))
            << 24U);
}

[[nodiscard]] std::uint16_t ReadU16(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1U]))
               << 8U);
}

[[nodiscard]] std::uint64_t ReadU64(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept
{
    std::uint64_t result = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        result |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                  << (index * 8U);
    }
    return result;
}

[[nodiscard]] bool IsKnownChunk(const std::uint32_t id) noexcept
{
    return id == VfstampChunkManf || id == VfstampChunkPal0 || id == VfstampChunkVox0 ||
           id == VfstampChunkHash || id == VfstampChunkThmb || id == VfstampChunkAnch ||
           id == VfstampChunkExtn;
}

[[nodiscard]] bool IsV1RequiredChunk(const std::uint32_t id) noexcept
{
    return id == VfstampChunkManf || id == VfstampChunkPal0 || id == VfstampChunkVox0 ||
           id == VfstampChunkHash;
}

[[nodiscard]] VfstampReadResult MakeError(
    const VfstampReadError error,
    const std::string_view message,
    const std::uint32_t chunkId = 0U) noexcept
{
    return {.Error = error, .ChunkId = chunkId, .Message = message};
}

void HashByte(std::uint64_t& hash, const std::byte byte) noexcept
{
    hash ^= std::to_integer<unsigned char>(byte);
    hash *= 1099511628211ULL;
}

template <typename T>
void HashLittleEndian(std::uint64_t& hash, const T value) noexcept
{
    for (std::size_t index = 0U; index < sizeof(T); ++index)
    {
        HashByte(hash, static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

} // namespace

std::uint32_t ComputeVfstampCrc32(const std::span<const std::byte> bytes) noexcept
{
    std::uint32_t crc = 0xffffffffU;
    for (const std::byte byte : bytes)
    {
        crc ^= std::to_integer<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit)
        {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

std::uint64_t ComputeVfstampStructuralHash(
    const std::uint16_t majorVersion,
    const std::uint16_t minorVersion,
    const std::span<const std::uint32_t> ids,
    const std::span<const std::uint32_t> flags,
    const std::span<const std::uint64_t> offsets,
    const std::span<const std::uint64_t> sizes,
    const std::span<const std::uint32_t> crcs) noexcept
{
    if (ids.size() != flags.size() || ids.size() != offsets.size() || ids.size() != sizes.size() ||
        ids.size() != crcs.size())
    {
        return 0U;
    }

    std::uint32_t nonHashCount = 0U;
    for (const std::uint32_t id : ids)
    {
        nonHashCount += id == VfstampChunkHash ? 0U : 1U;
    }

    std::uint64_t hash = 14695981039346656037ULL;
    HashLittleEndian(hash, majorVersion);
    HashLittleEndian(hash, minorVersion);
    HashLittleEndian(hash, nonHashCount);
    for (std::size_t index = 0U; index < ids.size(); ++index)
    {
        if (ids[index] == VfstampChunkHash)
        {
            continue;
        }
        HashLittleEndian(hash, ids[index]);
        HashLittleEndian(hash, flags[index]);
        HashLittleEndian(hash, offsets[index]);
        HashLittleEndian(hash, sizes[index]);
        HashLittleEndian(hash, crcs[index]);
    }
    return hash;
}

VfstampReadResult ReadVfstampBytes(
    const std::span<const std::byte> bytes,
    const StampResourceLimits& limits) noexcept
{
    try
    {
        if (bytes.empty())
        {
            return MakeError(VfstampReadError::EmptyInput, "Vfstamp input is empty.");
        }

        const StampLimitEvaluation fileLimits = EvaluateStampLimits(
            {.FileBytes = static_cast<std::uint64_t>(bytes.size())}, limits);
        if (!fileLimits.IsAllowed())
        {
            return MakeError(VfstampReadError::FileLimitExceeded,
                             "Vfstamp input exceeds the configured hard file-size limit.");
        }
        const bool softFileLimit = fileLimits.HasWarning();

        if (bytes.size() < VfstampHeaderSize)
        {
            return MakeError(VfstampReadError::FileTooSmall,
                             "Vfstamp input is shorter than its fixed header.");
        }
        if (!std::equal(VfstampMagic.begin(), VfstampMagic.end(),
                        bytes.begin() + VfstampHeaderMagicOffset))
        {
            return MakeError(VfstampReadError::InvalidMagic, "Vfstamp magic is invalid.");
        }

        const std::uint16_t majorVersion = ReadU16(bytes, VfstampHeaderMajorVersionOffset);
        const std::uint16_t minorVersion = ReadU16(bytes, VfstampHeaderMinorVersionOffset);
        const std::uint32_t headerFlags = ReadU32(bytes, VfstampHeaderFlagsOffset);
        const std::uint32_t chunkCount = ReadU32(bytes, VfstampHeaderChunkCountOffset);
        const std::uint64_t directoryOffset = ReadU32(bytes, VfstampHeaderDirectoryOffset);
        const std::uint64_t directorySize = ReadU32(bytes, VfstampHeaderDirectorySizeOffset);
        const std::uint32_t headerReserved = ReadU32(bytes, VfstampHeaderReservedOffset);

        if (majorVersion != VfstampFormatMajorVersion)
        {
            return MakeError(VfstampReadError::UnsupportedMajorVersion,
                             "Vfstamp major version is not supported.");
        }
        if (headerFlags != VfstampKnownHeaderFlags)
        {
            return MakeError(VfstampReadError::InvalidHeaderFlags,
                             "Vfstamp header contains unsupported flags.");
        }
        if (headerReserved != 0U)
        {
            return MakeError(VfstampReadError::InvalidReservedField,
                             "Vfstamp header reserved bytes must be zero in V1.");
        }

        const StampLimitEvaluation containerLimits = EvaluateStampLimits(
            {.FileBytes = static_cast<std::uint64_t>(bytes.size()), .ChunkCount = chunkCount},
            limits);
        if (!containerLimits.IsAllowed())
        {
            return MakeError(VfstampReadError::InvalidChunkCount,
                             "Vfstamp chunk count exceeds the configured hard limit.");
        }
        const bool softContainerLimit = containerLimits.HasWarning();

        if (MultiplyWouldOverflow(chunkCount, VfstampDirectoryEntrySize))
        {
            return MakeError(VfstampReadError::DirectorySizeOverflow,
                             "Vfstamp directory size arithmetic overflowed.");
        }
        const std::uint64_t expectedDirectorySize =
            static_cast<std::uint64_t>(chunkCount) * VfstampDirectoryEntrySize;
        if (directorySize != expectedDirectorySize || directoryOffset < VfstampHeaderSize ||
            AddWouldOverflow(directoryOffset, directorySize) ||
            directoryOffset + directorySize > bytes.size())
        {
            return MakeError(VfstampReadError::InvalidDirectoryBounds,
                             "Vfstamp directory is outside the input or has an invalid size.");
        }
        if (directoryOffset != VfstampHeaderSize)
        {
            return MakeError(VfstampReadError::UnreferencedBytes,
                             "Vfstamp contains bytes between its header and directory.");
        }

        std::vector<DirectoryEntry> entries;
        entries.reserve(chunkCount);
        std::array<bool, 4U> seenRequired{};
        std::uint64_t decodedPayloadBytes = 0U;
        for (std::uint32_t index = 0U; index < chunkCount; ++index)
        {
            const std::size_t entryOffset = static_cast<std::size_t>(directoryOffset) +
                                            static_cast<std::size_t>(index) *
                                                VfstampDirectoryEntrySize;
            const DirectoryEntry entry{
                .Id = ReadU32(bytes, entryOffset + VfstampDirectoryEntryIdOffset),
                .Flags = ReadU32(bytes, entryOffset + VfstampDirectoryEntryFlagsOffset),
                .Offset = ReadU64(bytes, entryOffset + VfstampDirectoryEntryPayloadOffset),
                .Size = ReadU64(bytes, entryOffset + VfstampDirectoryEntryPayloadSizeOffset),
                .Crc32 = ReadU32(bytes, entryOffset + VfstampDirectoryEntryCrc32Offset),
                .Reserved = ReadU32(bytes, entryOffset + VfstampDirectoryEntryReservedOffset)};
            if (entry.Reserved != 0U)
            {
                return MakeError(VfstampReadError::InvalidDirectoryEntry,
                                 "Vfstamp directory reserved bytes must be zero in V1.", entry.Id);
            }
            if ((entry.Flags & ~VfstampKnownChunkFlags) != 0U)
            {
                return MakeError(VfstampReadError::InvalidChunkFlags,
                                 "Vfstamp chunk contains unsupported flags.", entry.Id);
            }
            const bool carriesRequiredFlag =
                (entry.Flags & VfstampChunkFlagRequired) != 0U;
            if (carriesRequiredFlag && !IsV1RequiredChunk(entry.Id))
            {
                if (!IsKnownChunk(entry.Id))
                {
                    return MakeError(VfstampReadError::UnknownRequiredChunk,
                                     "Vfstamp contains an unknown required chunk.", entry.Id);
                }
                return MakeError(VfstampReadError::InvalidChunkFlags,
                                 "V1 optional chunks must not carry the required flag.", entry.Id);
            }
            if (IsV1RequiredChunk(entry.Id) && !carriesRequiredFlag)
            {
                return MakeError(VfstampReadError::InvalidChunkFlags,
                                 "Vfstamp required chunks must carry the required flag.", entry.Id);
            }
            if (entry.Offset < VfstampHeaderSize || AddWouldOverflow(entry.Offset, entry.Size) ||
                entry.Offset + entry.Size > bytes.size())
            {
                return MakeError(VfstampReadError::InvalidChunkBounds,
                                 "Vfstamp chunk payload is outside the input.", entry.Id);
            }
            if (AddWouldOverflow(decodedPayloadBytes, entry.Size))
            {
                return MakeError(VfstampReadError::DecodedSizeOverflow,
                                 "Vfstamp decoded payload size arithmetic overflowed.");
            }
            decodedPayloadBytes += entry.Size;
            entries.push_back(entry);
        }

        // A canonical V1 VOX0 begins with its declared record count followed by
        // fixed-size records. Inspect matching payloads directly in the validated
        // input span so a hostile hard-limit count is refused before the reader
        // allocates/copies any chunk payload representation. Structurally valid
        // containers with opaque VOX0 data remain the decoder's responsibility.
        bool softVoxelLimit = false;
        for (const DirectoryEntry& entry : entries)
        {
            if (entry.Id != VfstampChunkVox0 || entry.Size < sizeof(std::uint64_t))
            {
                continue;
            }
            const std::uint64_t voxelCount = ReadU64(
                bytes, static_cast<std::size_t>(entry.Offset));
            if (MultiplyWouldOverflow(voxelCount, VfstampVoxelRecordSize))
            {
                continue;
            }
            const std::uint64_t voxelBytes = voxelCount * VfstampVoxelRecordSize;
            if (AddWouldOverflow(sizeof(std::uint64_t), voxelBytes) ||
                sizeof(std::uint64_t) + voxelBytes != entry.Size)
            {
                continue;
            }
            const StampLimitEvaluation voxelLimits = EvaluateStampLimits(
                {.VoxelCount = voxelCount}, limits);
            if (!voxelLimits.IsAllowed())
            {
                return MakeError(
                    VfstampReadError::DecodedLimitExceeded,
                    "Vfstamp VOX0 record count exceeds the configured hard limit.",
                    entry.Id);
            }
            softVoxelLimit = voxelLimits.HasWarning();
            break;
        }

        const StampLimitEvaluation decodedLimits = EvaluateStampLimits(
            {.DecodedBytes = decodedPayloadBytes,
             .FileBytes = static_cast<std::uint64_t>(bytes.size()),
             .ChunkCount = chunkCount},
            limits);
        if (!decodedLimits.IsAllowed())
        {
            return MakeError(VfstampReadError::DecodedLimitExceeded,
                             "Vfstamp decoded payload size exceeds the configured hard limit.");
        }
        const bool softDecodedLimit = decodedLimits.HasWarning();

        const std::uint64_t directoryEnd = directoryOffset + directorySize;
        for (const DirectoryEntry& entry : entries)
        {
            const std::uint64_t entryEnd = entry.Offset + entry.Size;
            if (entry.Offset < directoryEnd && directoryOffset < entryEnd)
            {
                return MakeError(VfstampReadError::ChunkOverlapsContainer,
                                 "Vfstamp chunk payload overlaps the directory.", entry.Id);
            }
        }

        std::vector<std::size_t> order(entries.size());
        for (std::size_t index = 0U; index < order.size(); ++index)
        {
            order[index] = index;
        }
        std::sort(order.begin(), order.end(), [&entries](const std::size_t left,
                                                          const std::size_t right) {
            if (entries[left].Offset != entries[right].Offset)
            {
                return entries[left].Offset < entries[right].Offset;
            }
            return entries[left].Size < entries[right].Size;
        });
        for (std::size_t index = 1U; index < order.size(); ++index)
        {
            const DirectoryEntry& previous = entries[order[index - 1U]];
            const DirectoryEntry& current = entries[order[index]];
            if (previous.Size != 0U && current.Size != 0U &&
                previous.Offset + previous.Size > current.Offset)
            {
                return MakeError(VfstampReadError::ChunkOverlap,
                                 "Vfstamp chunk payloads overlap.");
            }
        }

        std::uint64_t payloadCursor = directoryEnd;
        for (const std::size_t index : order)
        {
            const DirectoryEntry& entry = entries[index];
            if (entry.Offset != payloadCursor)
            {
                return MakeError(VfstampReadError::UnreferencedBytes,
                                 "Vfstamp contains unreferenced bytes between payload ranges.");
            }
            payloadCursor += entry.Size;
        }
        if (payloadCursor != bytes.size())
        {
            return MakeError(VfstampReadError::UnreferencedBytes,
                             "Vfstamp contains unreferenced trailing bytes.");
        }

        std::array<std::uint32_t, 4U> requiredIds{
            VfstampChunkManf, VfstampChunkPal0, VfstampChunkVox0, VfstampChunkHash};
        for (const DirectoryEntry& entry : entries)
        {
            for (std::size_t requiredIndex = 0U; requiredIndex < requiredIds.size();
                 ++requiredIndex)
            {
                if (entry.Id == requiredIds[requiredIndex])
                {
                    if (seenRequired[requiredIndex])
                    {
                        return MakeError(VfstampReadError::DuplicateRequiredChunk,
                                         "Vfstamp contains a duplicate required chunk.",
                                         entry.Id);
                    }
                    seenRequired[requiredIndex] = true;
                }
            }
        }
        if (std::find(seenRequired.begin(), seenRequired.end(), false) != seenRequired.end())
        {
            return MakeError(VfstampReadError::MissingRequiredChunk,
                             "Vfstamp is missing one or more required chunks.");
        }

        std::vector<std::uint32_t> ids;
        std::vector<std::uint32_t> flags;
        std::vector<std::uint64_t> offsets;
        std::vector<std::uint64_t> sizes;
        std::vector<std::uint32_t> crcs;
        ids.reserve(entries.size());
        flags.reserve(entries.size());
        offsets.reserve(entries.size());
        sizes.reserve(entries.size());
        crcs.reserve(entries.size());
        const DirectoryEntry* hashEntry = nullptr;
        for (const DirectoryEntry& entry : entries)
        {
            const auto payload = bytes.subspan(static_cast<std::size_t>(entry.Offset),
                                               static_cast<std::size_t>(entry.Size));
            if (ComputeVfstampCrc32(payload) != entry.Crc32)
            {
                return MakeError(VfstampReadError::ChecksumMismatch,
                                 "Vfstamp chunk CRC32 does not match its payload.", entry.Id);
            }
            ids.push_back(entry.Id);
            flags.push_back(entry.Flags);
            offsets.push_back(entry.Offset);
            sizes.push_back(entry.Size);
            crcs.push_back(entry.Crc32);
            if (entry.Id == VfstampChunkHash)
            {
                hashEntry = &entry;
            }
        }

        if (hashEntry == nullptr || (hashEntry->Size != VfstampStructuralHashSize &&
                                     hashEntry->Size != VfstampHashPayloadSize))
        {
            return MakeError(VfstampReadError::StructuralHashMismatch,
                             "Vfstamp HASH chunk must contain an 8- or 16-byte V1 hash payload.",
                             VfstampChunkHash);
        }
        const auto hashPayload = bytes.subspan(static_cast<std::size_t>(hashEntry->Offset),
                                               VfstampStructuralHashSize);
        if (ReadU64(hashPayload, 0U) != ComputeVfstampStructuralHash(
                                        majorVersion, minorVersion, ids, flags, offsets, sizes,
                                        crcs))
        {
            return MakeError(VfstampReadError::StructuralHashMismatch,
                             "Vfstamp structural hash does not match its directory.",
                             VfstampChunkHash);
        }

        VfstampContainer container{
            .MajorVersion = majorVersion, .MinorVersion = minorVersion, .HeaderFlags = headerFlags};
        container.Chunks.reserve(entries.size());
        for (const DirectoryEntry& entry : entries)
        {
            const auto payload = bytes.subspan(static_cast<std::size_t>(entry.Offset),
                                               static_cast<std::size_t>(entry.Size));
            container.Chunks.push_back(
                {.Id = entry.Id, .Flags = entry.Flags, .Crc32 = entry.Crc32,
                 .Payload = std::vector<std::byte>(payload.begin(), payload.end())});
        }

        VfstampReadResult result{.Container = std::move(container)};
        result.Warnings.reserve(2U);
        if (minorVersion > VfstampFormatMinorVersion)
        {
            result.Warnings.push_back(VfstampReadWarning::NewerMinorVersion);
        }
        if (softFileLimit || softContainerLimit || softDecodedLimit ||
            softVoxelLimit)
        {
            result.Warnings.push_back(VfstampReadWarning::SoftResourceLimitExceeded);
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return MakeError(VfstampReadError::AllocationFailure,
                         "Vfstamp payload ownership could not be allocated.");
    }
    catch (const std::exception&)
    {
        return MakeError(VfstampReadError::AllocationFailure,
                         "Vfstamp payload ownership could not be represented safely.");
    }
}

} // namespace VoxelForge::Editor::Stamps
