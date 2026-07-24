#include "VoxelStamps/Format/VfstampReader.h"
#include "VoxelStamps/Format/VfstampWriter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace VoxelForge::Editor::Stamps;

namespace
{

struct FixtureChunk final
{
    std::uint32_t Id = 0U;
    std::uint32_t Flags = 0U;
    std::vector<std::byte> Payload;
};

void Check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void WriteU16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void WriteU64(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint64_t value)
{
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

std::uint32_t ReadU32(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::uint64_t ReadU64(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::vector<FixtureChunk> RequiredFixtureChunks()
{
    return {
        {.Id = VfstampChunkManf,
         .Flags = VfstampChunkFlagRequired,
         .Payload = {std::byte{1U}}},
        {.Id = VfstampChunkPal0,
         .Flags = VfstampChunkFlagRequired,
         .Payload = {std::byte{2U}, std::byte{3U}}},
        {.Id = VfstampChunkVox0,
         .Flags = VfstampChunkFlagRequired,
         .Payload = {std::byte{4U}, std::byte{5U}, std::byte{6U}}},
        {.Id = VfstampChunkHash,
         .Flags = VfstampChunkFlagRequired,
         .Payload = std::vector<std::byte>(VfstampStructuralHashSize)} };
}

// Test-fixture construction only.  STAMP-03 owns the production writer.
std::vector<std::byte> BuildFixture(
    std::vector<FixtureChunk> chunks,
    const std::uint16_t minorVersion = VfstampFormatMinorVersion)
{
    const std::size_t directoryOffset = VfstampHeaderSize;
    const std::size_t directorySize = chunks.size() * VfstampDirectoryEntrySize;
    std::size_t totalSize = directoryOffset + directorySize;
    for (const FixtureChunk& chunk : chunks)
    {
        totalSize += chunk.Payload.size();
    }
    std::vector<std::byte> bytes(totalSize, std::byte{0U});
    for (std::size_t index = 0U; index < VfstampMagic.size(); ++index)
    {
        bytes[index] = VfstampMagic[index];
    }
    WriteU16(bytes, 8U, VfstampFormatMajorVersion);
    WriteU16(bytes, 10U, minorVersion);
    WriteU32(bytes, 12U, 0U);
    WriteU32(bytes, 16U, static_cast<std::uint32_t>(chunks.size()));
    WriteU32(bytes, 20U, static_cast<std::uint32_t>(directoryOffset));
    WriteU32(bytes, 24U, static_cast<std::uint32_t>(directorySize));

    std::vector<std::uint32_t> ids;
    std::vector<std::uint32_t> flags;
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint64_t> sizes;
    std::vector<std::uint32_t> crcs;
    std::size_t payloadOffset = directoryOffset + directorySize;
    for (std::size_t index = 0U; index < chunks.size(); ++index)
    {
        const FixtureChunk& chunk = chunks[index];
        const std::size_t entryOffset = directoryOffset + index * VfstampDirectoryEntrySize;
        const std::uint32_t crc = ComputeVfstampCrc32(chunk.Payload);
        WriteU32(bytes, entryOffset, chunk.Id);
        WriteU32(bytes, entryOffset + 4U, chunk.Flags);
        WriteU64(bytes, entryOffset + 8U, payloadOffset);
        WriteU64(bytes, entryOffset + 16U, chunk.Payload.size());
        WriteU32(bytes, entryOffset + 24U, crc);
        for (std::size_t payloadIndex = 0U; payloadIndex < chunk.Payload.size(); ++payloadIndex)
        {
            bytes[payloadOffset + payloadIndex] = chunk.Payload[payloadIndex];
        }
        ids.push_back(chunk.Id);
        flags.push_back(chunk.Flags);
        offsets.push_back(payloadOffset);
        sizes.push_back(chunk.Payload.size());
        crcs.push_back(crc);
        payloadOffset += chunk.Payload.size();
    }

    for (std::size_t index = 0U; index < chunks.size(); ++index)
    {
        if (chunks[index].Id != VfstampChunkHash)
        {
            continue;
        }
        const std::size_t entryOffset = directoryOffset + index * VfstampDirectoryEntrySize;
        Check(chunks[index].Payload.size() == VfstampStructuralHashSize,
              "fixture HASH chunk must have its V1 size");
        const std::uint64_t hash = ComputeVfstampStructuralHash(
            VfstampFormatMajorVersion, minorVersion, ids, flags, offsets, sizes, crcs);
        const std::size_t hashOffset = static_cast<std::size_t>(offsets[index]);
        WriteU64(bytes, hashOffset, hash);
        WriteU32(bytes, entryOffset + 24U,
                 ComputeVfstampCrc32(std::span<const std::byte>(
                     bytes.data() + hashOffset, VfstampStructuralHashSize)));
    }
    return bytes;
}

constexpr std::size_t DirectoryOffset = VfstampHeaderSize;
constexpr std::size_t EntryOffset(const std::size_t index) noexcept
{
    return DirectoryOffset + index * VfstampDirectoryEntrySize;
}

constexpr std::size_t RequiredPayloadOffset =
    VfstampHeaderSize + 4U * VfstampDirectoryEntrySize;
constexpr std::size_t HashPayloadOffset = RequiredPayloadOffset + 1U + 2U + 3U;

VoxelStamp CanonicalStamp(const bool reverseVoxels = false, std::string contentHash = {})
{
    std::vector<StampVoxel> voxels{
        {.Position = {.X = 0, .Y = 0, .Z = 0}, .LocalColorId = 0U},
        {.Position = {.X = 1, .Y = 1, .Z = 1}, .LocalColorId = 1U}};
    if (reverseVoxels)
    {
        std::swap(voxels[0], voxels[1]);
    }
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = VoxelForge::Core::UUID{42U}, .ContentHash = std::move(contentHash)},
        {.Minimum = {}, .Maximum = {.X = 1, .Y = 1, .Z = 1}, .Dimensions = {.X = 2U, .Y = 2U, .Z = 2U}},
        {.RequestedMode = StampPivotMode::Auto,
         .ResolvedMode = StampPivotMode::BottomCenter,
         .LocalPosition = {.X = 256, .Y = 0, .Z = 256},
         .LocalNormal = {.X = 0, .Y = 1, .Z = 0},
         .AutoPolicyVersion = 1U},
        {},
        {{.LocalColorId = 0U, .Color = {.Red = 255U, .Green = 0U, .Blue = 0U, .Alpha = 255U}},
         {.LocalColorId = 1U, .Color = {.Red = 0U, .Green = 255U, .Blue = 0U, .Alpha = 255U}}},
        std::move(voxels));
    Check(stamp.has_value(), "canonical STAMP-03 fixture");
    return *stamp;
}

VoxelStamp LargeStamp()
{
    std::vector<StampVoxel> voxels;
    voxels.reserve(64U * 64U);
    for (std::int32_t y = 0; y < 64; ++y)
    {
        for (std::int32_t x = 0; x < 64; ++x)
        {
            voxels.push_back({.Position = {.X = x, .Y = y, .Z = 0}, .LocalColorId = 0U});
        }
    }
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = VoxelForge::Core::UUID{99U}},
        {.Minimum = {}, .Maximum = {.X = 63, .Y = 63, .Z = 0}, .Dimensions = {.X = 64U, .Y = 64U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {.X = 8192, .Y = 8192, .Z = 0},
         .LocalNormal = {},
         .AutoPolicyVersion = 0U},
        {},
        {{.LocalColorId = 0U, .Color = {.Red = 1U, .Green = 2U, .Blue = 3U, .Alpha = 255U},
          .HasSourcePaletteIndex = true, .SourcePaletteIndex = 17U}},
        std::move(voxels));
    Check(stamp.has_value(), "large STAMP-03 fixture");
    return *stamp;
}

VoxelStamp MaximumPaletteStamp(std::string contentHash = {})
{
    std::vector<StampPaletteEntry> palette;
    std::vector<StampVoxel> voxels;
    palette.reserve(256U);
    voxels.reserve(256U);
    for (std::uint32_t index = 0U; index < 256U; ++index)
    {
        const std::uint8_t colorId = static_cast<std::uint8_t>(index);
        palette.push_back(
            {.LocalColorId = colorId,
             .Color = {.Red = colorId,
                       .Green = static_cast<std::uint8_t>(255U - index),
                       .Blue = static_cast<std::uint8_t>(index ^ 0x5aU),
                       .Alpha = 255U},
             .HasSourcePaletteIndex = true,
             .SourcePaletteIndex = colorId});
        voxels.push_back(
            {.Position = {.X = static_cast<std::int32_t>(index), .Y = 0, .Z = 0},
             .LocalColorId = colorId});
    }
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = VoxelForge::Core::UUID{256U}, .ContentHash = std::move(contentHash)},
        {.Minimum = {}, .Maximum = {.X = 255, .Y = 0, .Z = 0}, .Dimensions = {.X = 256U, .Y = 1U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {.X = 32768, .Y = 0, .Z = 0},
         .LocalNormal = {},
         .AutoPolicyVersion = 0U},
        {},
        std::move(palette),
        std::move(voxels));
    Check(stamp.has_value(), "maximum-palette STAMP-03 fixture");
    return *stamp;
}

} // namespace

int main()
{
    try
    {
        const std::array<std::byte, 9U> standardCrcInput{
            static_cast<std::byte>('1'), static_cast<std::byte>('2'), static_cast<std::byte>('3'),
            static_cast<std::byte>('4'), static_cast<std::byte>('5'), static_cast<std::byte>('6'),
            static_cast<std::byte>('7'), static_cast<std::byte>('8'), static_cast<std::byte>('9')};
        Check(ComputeVfstampCrc32(standardCrcInput) == 0xcbf43926U,
              "CRC32 matches the standard 123456789 vector");
        Check(!VfstampReadResult{}.IsSuccess(), "a default result is not a successful read");

        const std::vector<std::byte> valid = BuildFixture(RequiredFixtureChunks());
        const VfstampReadResult validResult = ReadVfstampBytes(valid);
        Check(validResult.IsSuccess() && validResult.Container.has_value(), "minimum valid V1 file");
        Check(validResult.Container->Chunks.size() == 4U, "owned required chunk payloads");
        Check(validResult.Container->Chunks[0].Payload[0] == std::byte{1U},
              "reader preserves payload bytes");

        const VoxelStamp initiallyUnhashedStamp = CanonicalStamp();
        const std::string canonicalHash = CalculateVfstampLogicalContentHash(initiallyUnhashedStamp);
        Check(canonicalHash == "96eced80e0c9fd9e",
              "logical hash golden value is stable across environments");
        const VoxelStamp canonicalStamp = CanonicalStamp(false, canonicalHash);
        const VfstampWriteResult firstWrite = WriteVfstampBytes(canonicalStamp);
        Check(firstWrite.IsSuccess() && firstWrite.LogicalContentHash == canonicalHash,
              "canonical writer output and produced logical hash");
        const VfstampReadResult writtenRead = ReadVfstampBytes(firstWrite.Bytes);
        Check(writtenRead.IsSuccess() && writtenRead.Container->Chunks.size() == 4U &&
                  writtenRead.Container->Chunks[0].Id == VfstampChunkManf &&
                  writtenRead.Container->Chunks[1].Id == VfstampChunkPal0 &&
                  writtenRead.Container->Chunks[2].Id == VfstampChunkVox0 &&
                  writtenRead.Container->Chunks[3].Id == VfstampChunkHash,
              "writer canonical chunk order");
        for (std::size_t index = 0U; index < 4U; ++index)
        {
            const std::size_t directoryOffset =
                VfstampHeaderSize + index * VfstampDirectoryEntrySize;
            const std::uint64_t payloadOffset = ReadU64(
                firstWrite.Bytes, directoryOffset + VfstampDirectoryEntryPayloadOffset);
            const std::uint64_t payloadSize = ReadU64(
                firstWrite.Bytes, directoryOffset + VfstampDirectoryEntryPayloadSizeOffset);
            const std::uint32_t directoryCrc = ReadU32(
                firstWrite.Bytes, directoryOffset + VfstampDirectoryEntryCrc32Offset);
            Check(ComputeVfstampCrc32(std::span<const std::byte>(
                      firstWrite.Bytes.data() + static_cast<std::size_t>(payloadOffset),
                      static_cast<std::size_t>(payloadSize))) == directoryCrc,
                  "writer directory CRC matches every payload");
        }
        const VfstampDecodeResult decoded = DecodeVfstampBytes(firstWrite.Bytes);
        Check(decoded.IsSuccess() && *decoded.Stamp == canonicalStamp &&
                  decoded.Stamp->Identity().ContentHash == CalculateVfstampLogicalContentHash(*decoded.Stamp),
              "writer to reader to TryCreate domain round trip");
        const VfstampWriteResult secondWrite = WriteVfstampBytes(*decoded.Stamp);
        Check(secondWrite.IsSuccess() && secondWrite.Bytes == firstWrite.Bytes,
              "read write read write bytes are stable");
        const VfstampDecodeResult decodedAgain = DecodeVfstampBytes(secondWrite.Bytes);
        Check(decodedAgain.IsSuccess(), "second writer output decodes");
        const VfstampWriteResult thirdWrite = WriteVfstampBytes(*decodedAgain.Stamp);
        Check(thirdWrite.IsSuccess() && thirdWrite.Bytes == firstWrite.Bytes,
              "serialize read serialize read serialize is byte-stable");
        const VfstampWriteResult reorderedWrite = WriteVfstampBytes(CanonicalStamp(true));
        Check(reorderedWrite.IsSuccess() && reorderedWrite.Bytes == firstWrite.Bytes,
              "voxel insertion order does not affect canonical bytes");
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            Check(WriteVfstampBytes(canonicalStamp).Bytes == firstWrite.Bytes,
                  "repeated canonical writes are byte-identical");
        }
        const VfstampWriteResult mismatchedHashWrite =
            WriteVfstampBytes(CanonicalStamp(false, "incoherent"));
        Check(!mismatchedHashWrite.IsSuccess() &&
                  mismatchedHashWrite.Error == VfstampWriteError::ContentHashMismatch &&
                  mismatchedHashWrite.Bytes.empty(),
              "mismatched logical content hash is rejected without bytes");
        auto writerCorruption = firstWrite.Bytes;
        writerCorruption.back() ^= std::byte{1U};
        Check(!ReadVfstampBytes(writerCorruption).IsSuccess(),
              "post-write corruption is detected by the reader");
        VfstampContainer logicalHashCorruption = *writtenRead.Container;
        logicalHashCorruption.Chunks[3].Payload[VfstampStructuralHashSize] ^= std::byte{1U};
        Check(DecodeVfstampContainer(logicalHashCorruption).Error ==
                  VfstampDecodeError::LogicalHashMismatch,
              "logical HASH corruption is rejected by the decoder");
        VfstampContainer malformedManifest = *writtenRead.Container;
        malformedManifest.Chunks[0].Payload[0] = std::byte{0U};
        Check(DecodeVfstampContainer(malformedManifest).Error == VfstampDecodeError::InvalidManifest,
              "malformed MANF diagnostic");
        VfstampContainer malformedPalette = *writtenRead.Container;
        malformedPalette.Chunks[1].Payload.pop_back();
        Check(DecodeVfstampContainer(malformedPalette).Error == VfstampDecodeError::InvalidPalette,
              "malformed PAL0 diagnostic");
        VfstampContainer negativeVoxel = *writtenRead.Container;
        for (std::size_t index = 0U; index < 4U; ++index)
        {
            negativeVoxel.Chunks[2].Payload[8U + index] = std::byte{0xffU};
        }
        Check(DecodeVfstampContainer(negativeVoxel).Error ==
                  VfstampDecodeError::DomainValidationFailed,
              "negative local coordinate is rejected by TryCreate invariants");
        const VfstampWriteResult largeWrite = WriteVfstampBytes(LargeStamp());
        const VfstampDecodeResult largeDecode = DecodeVfstampBytes(largeWrite.Bytes);
        Check(largeWrite.IsSuccess() && largeDecode.IsSuccess() &&
                  largeDecode.Stamp->Voxels().size() == 4096U &&
                  largeDecode.Stamp->Palette()[0].HasSourcePaletteIndex &&
                  largeDecode.Stamp->Palette()[0].SourcePaletteIndex == 17U,
              "large multi-voxel stamp round-trips under V1 limits");
        const VoxelStamp unhashMaximumPaletteStamp = MaximumPaletteStamp();
        const VoxelStamp maximumPaletteStamp = MaximumPaletteStamp(
            CalculateVfstampLogicalContentHash(unhashMaximumPaletteStamp));
        const VfstampWriteResult maximumPaletteWrite = WriteVfstampBytes(maximumPaletteStamp);
        const VfstampDecodeResult maximumPaletteDecode =
            DecodeVfstampBytes(maximumPaletteWrite.Bytes);
        Check(maximumPaletteWrite.IsSuccess() && maximumPaletteDecode.IsSuccess() &&
                  *maximumPaletteDecode.Stamp == maximumPaletteStamp &&
                  maximumPaletteDecode.Stamp->Palette().size() == 256U &&
                  maximumPaletteDecode.Stamp->Voxels().size() == 256U,
              "maximum V1 palette with every color referenced round-trips exactly");
        StampResourceLimits writerVoxelLimit = DefaultStampResourceLimits();
        writerVoxelLimit.SoftVoxelCount = 1U;
        writerVoxelLimit.HardVoxelCount = 1U;
        const VfstampWriteResult voxelLimitWrite = WriteVfstampBytes(canonicalStamp, writerVoxelLimit);
        Check(!voxelLimitWrite.IsSuccess() &&
                  voxelLimitWrite.Error == VfstampWriteError::InvalidStamp &&
                  voxelLimitWrite.Bytes.empty(),
              "writer voxel hard limit produces no bytes");
        StampResourceLimits writerFileLimit = DefaultStampResourceLimits();
        writerFileLimit.SoftFileBytes = 1U;
        writerFileLimit.HardFileBytes = 1U;
        const VfstampWriteResult fileLimitWrite = WriteVfstampBytes(canonicalStamp, writerFileLimit);
        Check(!fileLimitWrite.IsSuccess() && fileLimitWrite.Error == VfstampWriteError::ResourceLimitExceeded &&
                  fileLimitWrite.Bytes.empty(),
              "writer file hard limit produces no bytes");
        StampResourceLimits writerDecodedLimit = DefaultStampResourceLimits();
        writerDecodedLimit.SoftDecodedBytes = 1U;
        writerDecodedLimit.HardDecodedBytes = 1U;
        const VfstampWriteResult decodedLimitWrite =
            WriteVfstampBytes(canonicalStamp, writerDecodedLimit);
        Check(!decodedLimitWrite.IsSuccess() &&
                  decodedLimitWrite.Error == VfstampWriteError::InvalidStamp &&
                  decodedLimitWrite.Bytes.empty(),
              "writer decoded hard limit produces no bytes");
        auto largeChunks = RequiredFixtureChunks();
        largeChunks[2U].Payload.assign(StampResourceLimits::Mebibyte, std::byte{0x5aU});
        const VfstampReadResult largeResult = ReadVfstampBytes(BuildFixture(std::move(largeChunks)));
        Check(largeResult.IsSuccess() &&
                  largeResult.Container->Chunks[2U].Payload.size() == StampResourceLimits::Mebibyte,
              "one-mebibyte VOX0 payload is read from memory");

        const VfstampReadResult emptyResult = ReadVfstampBytes(std::span<const std::byte>{});
        Check(emptyResult.Error == VfstampReadError::EmptyInput && !emptyResult.Container.has_value(),
              "empty input has no partial container");
        for (std::size_t length = 0U; length < valid.size(); ++length)
        {
            const VfstampReadResult result = ReadVfstampBytes(
                std::span<const std::byte>(valid.data(), length));
            Check(!result.IsSuccess() && !result.Container.has_value(),
                  "every strict prefix is rejected without partial output");
        }

        auto invalidMagic = valid;
        invalidMagic[0] = std::byte{0U};
        Check(ReadVfstampBytes(invalidMagic).Error == VfstampReadError::InvalidMagic, "magic");
        auto invalidMajor = valid;
        WriteU16(invalidMajor, 8U, VfstampFormatMajorVersion + 1U);
        Check(ReadVfstampBytes(invalidMajor).Error == VfstampReadError::UnsupportedMajorVersion,
              "major version");
        const VfstampReadResult newerMinor =
            ReadVfstampBytes(BuildFixture(RequiredFixtureChunks(), VfstampFormatMinorVersion + 1U));
        Check(newerMinor.IsSuccess() && newerMinor.Warnings.size() == 1U &&
                  newerMinor.Warnings[0] == VfstampReadWarning::NewerMinorVersion,
              "newer minor version is accepted with warning");
        auto invalidFlags = valid;
        WriteU32(invalidFlags, 12U, 1U);
        Check(ReadVfstampBytes(invalidFlags).Error == VfstampReadError::InvalidHeaderFlags,
              "header flags");
        auto invalidHeaderReserved = valid;
        WriteU32(invalidHeaderReserved, 28U, 1U);
        Check(ReadVfstampBytes(invalidHeaderReserved).Error == VfstampReadError::InvalidReservedField,
              "header reserved field");
        auto invalidDirectory = valid;
        WriteU32(invalidDirectory, 24U, 0U);
        Check(ReadVfstampBytes(invalidDirectory).Error == VfstampReadError::InvalidDirectoryBounds,
              "directory size");
        auto directoryPadding = valid;
        WriteU32(directoryPadding, VfstampHeaderDirectoryOffset, VfstampHeaderSize + 1U);
        Check(ReadVfstampBytes(directoryPadding).Error == VfstampReadError::UnreferencedBytes,
              "padding before directory is rejected");
        auto invalidEntryReserved = valid;
        WriteU32(invalidEntryReserved, EntryOffset(0U) + 28U, 1U);
        Check(ReadVfstampBytes(invalidEntryReserved).Error == VfstampReadError::InvalidDirectoryEntry,
              "directory entry reserved field");
        auto invalidChunkFlags = valid;
        WriteU32(invalidChunkFlags, EntryOffset(0U) + 4U, 2U);
        Check(ReadVfstampBytes(invalidChunkFlags).Error == VfstampReadError::InvalidChunkFlags,
              "chunk flags");
        for (std::size_t requiredIndex = 0U; requiredIndex < 4U; ++requiredIndex)
        {
            auto requiredFlagMissing = valid;
            WriteU32(requiredFlagMissing, EntryOffset(requiredIndex) + 4U, 0U);
            Check(ReadVfstampBytes(requiredFlagMissing).Error == VfstampReadError::InvalidChunkFlags,
                  "every required chunk must carry the required flag");
        }

        auto invalidOffset = valid;
        WriteU64(invalidOffset, EntryOffset(0U) + 8U, 1U);
        Check(ReadVfstampBytes(invalidOffset).Error == VfstampReadError::InvalidChunkBounds,
              "payload offset below container");
        auto directoryOverlap = valid;
        WriteU64(directoryOverlap, EntryOffset(0U) + 8U, VfstampHeaderSize);
        Check(ReadVfstampBytes(directoryOverlap).Error == VfstampReadError::ChunkOverlapsContainer,
              "payload overlaps directory");
        auto invalidSize = valid;
        WriteU64(invalidSize, EntryOffset(0U) + 16U, std::numeric_limits<std::uint64_t>::max());
        Check(ReadVfstampBytes(invalidSize).Error == VfstampReadError::InvalidChunkBounds,
              "payload size overflow");
        auto overlap = valid;
        WriteU64(overlap, EntryOffset(1U) + 8U,
                 static_cast<std::uint64_t>(VfstampHeaderSize + 4U * VfstampDirectoryEntrySize));
        Check(ReadVfstampBytes(overlap).Error == VfstampReadError::ChunkOverlap, "payload overlap");
        auto interPayloadGap = valid;
        interPayloadGap.push_back(std::byte{0U});
        WriteU64(interPayloadGap, EntryOffset(1U) + VfstampDirectoryEntryPayloadOffset,
                 RequiredPayloadOffset + 2U);
        WriteU64(interPayloadGap, EntryOffset(2U) + VfstampDirectoryEntryPayloadOffset,
                 RequiredPayloadOffset + 4U);
        WriteU64(interPayloadGap, EntryOffset(3U) + VfstampDirectoryEntryPayloadOffset,
                 RequiredPayloadOffset + 7U);
        Check(ReadVfstampBytes(interPayloadGap).Error == VfstampReadError::UnreferencedBytes,
              "gap between payload ranges is rejected");
        auto trailingByte = valid;
        trailingByte.push_back(std::byte{0U});
        Check(ReadVfstampBytes(trailingByte).Error == VfstampReadError::UnreferencedBytes,
              "unreferenced trailing byte is rejected");

        auto duplicateChunks = RequiredFixtureChunks();
        duplicateChunks.push_back(
            {.Id = VfstampChunkManf, .Flags = VfstampChunkFlagRequired, .Payload = {std::byte{9U}}});
        Check(ReadVfstampBytes(BuildFixture(std::move(duplicateChunks))).Error ==
                  VfstampReadError::DuplicateRequiredChunk,
              "duplicate required chunk");
        auto missingChunks = RequiredFixtureChunks();
        missingChunks.erase(missingChunks.begin() + 2);
        Check(ReadVfstampBytes(BuildFixture(std::move(missingChunks))).Error ==
                  VfstampReadError::MissingRequiredChunk,
              "missing required chunk");
        auto optionalChunks = RequiredFixtureChunks();
        optionalChunks.push_back(
            {.Id = MakeVfstampChunkId('U', 'N', 'K', 'N'), .Payload = {std::byte{7U}}});
        const VfstampReadResult optionalResult = ReadVfstampBytes(BuildFixture(optionalChunks));
        Check(optionalResult.IsSuccess() && optionalResult.Container->Chunks.size() == 5U &&
                  optionalResult.Container->Chunks.back().Id == MakeVfstampChunkId('U', 'N', 'K', 'N'),
              "unknown optional chunk is validated then preserved as opaque data");
        auto emptyOptionalChunks = RequiredFixtureChunks();
        emptyOptionalChunks.push_back({.Id = VfstampChunkThmb, .Flags = 0U, .Payload = {}});
        const VfstampReadResult emptyOptionalResult =
            ReadVfstampBytes(BuildFixture(std::move(emptyOptionalChunks)));
        Check(emptyOptionalResult.IsSuccess() && emptyOptionalResult.Container->Chunks.size() == 5U &&
                  emptyOptionalResult.Container->Chunks.back().Payload.empty(),
              "empty optional chunk at the payload frontier is accepted");
        auto requiredThumbnailChunks = RequiredFixtureChunks();
        requiredThumbnailChunks.push_back(
            {.Id = VfstampChunkThmb,
             .Flags = VfstampChunkFlagRequired,
             .Payload = {std::byte{9U}}});
        Check(ReadVfstampBytes(BuildFixture(std::move(requiredThumbnailChunks))).Error ==
                  VfstampReadError::InvalidChunkFlags,
              "V1 thumbnail remains optional");
        optionalChunks.back().Flags = VfstampChunkFlagRequired;
        Check(ReadVfstampBytes(BuildFixture(std::move(optionalChunks))).Error ==
                  VfstampReadError::UnknownRequiredChunk,
              "unknown required chunk");

        auto corruptChecksum = valid;
        corruptChecksum.back() ^= std::byte{1U};
        Check(ReadVfstampBytes(corruptChecksum).Error == VfstampReadError::ChecksumMismatch,
              "checksum corruption");
        auto corruptHash = valid;
        corruptHash[EntryOffset(0U) + 24U] ^= std::byte{1U};
        Check(ReadVfstampBytes(corruptHash).Error == VfstampReadError::ChecksumMismatch,
              "directory checksum corruption");
        auto structuralHashChunks = RequiredFixtureChunks();
        structuralHashChunks.push_back(
            {.Id = VfstampChunkThmb, .Flags = 0U, .Payload = {std::byte{8U}}});
        auto structuralHashMismatch = BuildFixture(std::move(structuralHashChunks));
        WriteU32(structuralHashMismatch, EntryOffset(4U), VfstampChunkAnch);
        Check(ReadVfstampBytes(structuralHashMismatch).Error ==
                  VfstampReadError::StructuralHashMismatch,
              "structural hash corruption");
        auto invalidHashSize = valid;
        WriteU64(invalidHashSize, EntryOffset(3U) + 16U, VfstampStructuralHashSize - 1U);
        WriteU32(invalidHashSize, EntryOffset(3U) + 24U,
                 ComputeVfstampCrc32(std::span<const std::byte>(
                     invalidHashSize.data() + HashPayloadOffset, VfstampStructuralHashSize - 1U)));
        invalidHashSize.pop_back();
        Check(ReadVfstampBytes(invalidHashSize).Error == VfstampReadError::StructuralHashMismatch,
              "HASH must have its fixed size");

        StampResourceLimits smallChunkLimit = DefaultStampResourceLimits();
        smallChunkLimit.SoftChunkCount = 3U;
        smallChunkLimit.HardChunkCount = 3U;
        Check(ReadVfstampBytes(valid, smallChunkLimit).Error == VfstampReadError::InvalidChunkCount,
              "hard chunk-count limit before allocations");
        StampResourceLimits smallFileLimit = DefaultStampResourceLimits();
        smallFileLimit.SoftFileBytes = 1U;
        smallFileLimit.HardFileBytes = static_cast<std::uint64_t>(valid.size() - 1U);
        Check(ReadVfstampBytes(valid, smallFileLimit).Error == VfstampReadError::FileLimitExceeded,
              "file hard limit before parsing or allocation");
        StampResourceLimits decodedAtHardLimit = DefaultStampResourceLimits();
        decodedAtHardLimit.SoftDecodedBytes = 14U;
        decodedAtHardLimit.HardDecodedBytes = 14U;
        Check(ReadVfstampBytes(valid, decodedAtHardLimit).IsSuccess(),
              "decoded size exactly at hard limit is accepted");
        StampResourceLimits softDecodedLimit = decodedAtHardLimit;
        softDecodedLimit.SoftDecodedBytes = 13U;
        const VfstampReadResult softDecodedResult = ReadVfstampBytes(valid, softDecodedLimit);
        Check(softDecodedResult.IsSuccess() && softDecodedResult.Warnings.size() == 1U &&
                  softDecodedResult.Warnings[0] == VfstampReadWarning::SoftResourceLimitExceeded,
              "decoded soft limit warns after validation");
        StampResourceLimits hardDecodedLimit = decodedAtHardLimit;
        hardDecodedLimit.SoftDecodedBytes = 13U;
        hardDecodedLimit.HardDecodedBytes = 13U;
        const VfstampReadResult hardDecodedResult = ReadVfstampBytes(valid, hardDecodedLimit);
        Check(hardDecodedResult.Error == VfstampReadError::DecodedLimitExceeded &&
                  !hardDecodedResult.Container.has_value(),
              "decoded hard limit rejects before payload copies");
        auto hugeDeclaredPayload = valid;
        WriteU64(hugeDeclaredPayload, EntryOffset(0U) + 16U, 1024ULL * 1024ULL * 1024ULL);
        Check(ReadVfstampBytes(hugeDeclaredPayload).Error == VfstampReadError::InvalidChunkBounds,
              "large declared payload rejected without allocation");

        for (int iteration = 0; iteration < 8; ++iteration)
        {
            const VfstampReadResult repeated = ReadVfstampBytes(valid);
            Check(repeated.IsSuccess() && repeated.Container->Chunks == validResult.Container->Chunks,
                  "repeated reads are deterministic");
        }

        std::cout << "Vfstamp format tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
