#include "VoxelStamps/Validation/StampValidationService.h"

#include <iostream>
#include <stdexcept>
#include <array>
#include <algorithm>

using namespace VoxelForge::Editor::Stamps;

namespace
{
void Check(const bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::uint32_t ReadU32(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    return value;
}

std::uint64_t ReadU64(const std::vector<std::byte>& bytes, const std::size_t offset)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    return value;
}

void WriteU32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (std::size_t index = 0U; index < 4U; ++index)
        bytes[offset + index] = static_cast<std::byte>(value >> (index * 8U));
}

void WriteU64(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint64_t value)
{
    for (std::size_t index = 0U; index < 8U; ++index)
        bytes[offset + index] = static_cast<std::byte>(value >> (index * 8U));
}

void RecomputeV1Hashes(std::vector<std::byte>& bytes)
{
    const std::uint16_t major = static_cast<std::uint16_t>(ReadU32(bytes, 8U) & 0xffffU);
    const std::uint16_t minor = static_cast<std::uint16_t>(ReadU32(bytes, 10U) & 0xffffU);
    const std::uint32_t count = ReadU32(bytes, VfstampHeaderChunkCountOffset);
    const std::size_t directory = ReadU32(bytes, VfstampHeaderDirectoryOffset);
    std::vector<std::uint32_t> ids, flags, crcs;
    std::vector<std::uint64_t> offsets, sizes;
    std::size_t hashEntry = 0U;
    for (std::size_t index = 0U; index < count; ++index)
    {
        const std::size_t entry = directory + index * VfstampDirectoryEntrySize;
        ids.push_back(ReadU32(bytes, entry));
        flags.push_back(ReadU32(bytes, entry + VfstampDirectoryEntryFlagsOffset));
        offsets.push_back(ReadU64(bytes, entry + VfstampDirectoryEntryPayloadOffset));
        sizes.push_back(ReadU64(bytes, entry + VfstampDirectoryEntryPayloadSizeOffset));
        crcs.push_back(ReadU32(bytes, entry + VfstampDirectoryEntryCrc32Offset));
        if (ids.back() == VfstampChunkHash) hashEntry = index;
    }
    const std::uint64_t structural = ComputeVfstampStructuralHash(major, minor, ids, flags, offsets, sizes, crcs);
    WriteU64(bytes, static_cast<std::size_t>(offsets[hashEntry]), structural);
    const std::size_t hashDirectory = directory + hashEntry * VfstampDirectoryEntrySize;
    WriteU32(bytes, hashDirectory + VfstampDirectoryEntryCrc32Offset,
             ComputeVfstampCrc32(std::span<const std::byte>(
                 bytes.data() + static_cast<std::size_t>(offsets[hashEntry]),
                 static_cast<std::size_t>(sizes[hashEntry]))));
}

std::vector<std::byte> AddUnknownOptionalChunk(const std::vector<std::byte>& input)
{
    const std::size_t oldDirectory = VfstampHeaderSize;
    const std::size_t oldPayload = VfstampHeaderSize + 4U * VfstampDirectoryEntrySize;
    std::vector<std::byte> bytes(input.size() + VfstampDirectoryEntrySize + 1U, std::byte{0U});
    std::copy(input.begin(), input.begin() + VfstampHeaderSize, bytes.begin());
    std::copy(input.begin() + oldDirectory, input.begin() + oldPayload,
              bytes.begin() + VfstampHeaderSize);
    std::copy(input.begin() + oldPayload, input.end(),
              bytes.begin() + oldPayload + VfstampDirectoryEntrySize);
    WriteU32(bytes, VfstampHeaderChunkCountOffset, 5U);
    WriteU32(bytes, VfstampHeaderDirectorySizeOffset, 5U * VfstampDirectoryEntrySize);
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        const std::size_t entry = VfstampHeaderSize + index * VfstampDirectoryEntrySize;
        WriteU64(bytes, entry + VfstampDirectoryEntryPayloadOffset,
                 ReadU64(bytes, entry + VfstampDirectoryEntryPayloadOffset) +
                     VfstampDirectoryEntrySize);
    }
    const std::size_t unknown = VfstampHeaderSize + 4U * VfstampDirectoryEntrySize;
    const std::uint64_t unknownOffset = bytes.size() - 1U;
    WriteU32(bytes, unknown, MakeVfstampChunkId('U', 'N', 'K', 'N'));
    WriteU64(bytes, unknown + VfstampDirectoryEntryPayloadOffset, unknownOffset);
    WriteU64(bytes, unknown + VfstampDirectoryEntryPayloadSizeOffset, 1U);
    bytes.back() = std::byte{7U};
    WriteU32(bytes, unknown + VfstampDirectoryEntryCrc32Offset,
             ComputeVfstampCrc32(std::span<const std::byte>(bytes.data() + unknownOffset, 1U)));
    RecomputeV1Hashes(bytes);
    return bytes;
}

VoxelStamp MakeStamp(std::string hash = {})
{
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = VoxelForge::Core::UUID{17U}, .ContentHash = std::move(hash)},
        {.Minimum = {}, .Maximum = {.X = 1, .Y = 1, .Z = 1}, .Dimensions = {.X = 2U, .Y = 2U, .Z = 2U}},
        {.RequestedMode = StampPivotMode::Auto,
         .ResolvedMode = StampPivotMode::BottomCenter,
         .LocalPosition = {.X = 256, .Y = 0, .Z = 256},
         .LocalNormal = {.X = 0, .Y = 1, .Z = 0},
         .AutoPolicyVersion = 1U},
        {},
        {{.LocalColorId = 0U, .Color = {.Red = 1U, .Green = 2U, .Blue = 3U, .Alpha = 255U}},
         {.LocalColorId = 1U, .Color = {.Red = 4U, .Green = 5U, .Blue = 6U, .Alpha = 255U}}},
        {{.Position = {.X = 0, .Y = 0, .Z = 0}, .LocalColorId = 0U},
         {.Position = {.X = 1, .Y = 1, .Z = 1}, .LocalColorId = 1U}});
    Check(stamp.has_value(), "validation fixture");
    return *stamp;
}
}

int main()
{
    try
    {
        const VoxelStamp unhashed = MakeStamp();
        const VoxelStamp stamp = MakeStamp(CalculateVfstampLogicalContentHash(unhashed));
        Check(ValidateStamp(stamp).IsValid(), "semantic validation");
        Check(ValidateStampForPlacement(stamp).IsValid(), "placement semantic validation");
        const VfstampWriteResult write = WriteVfstampBytes(stamp);
        Check(write.IsSuccess(), "writer invokes central validation");
        const VfstampInspection inspection = InspectVfstampBytes(write.Bytes);
        Check(ValidateVfstampBytes(write.Bytes) == inspection.Report,
              "explicit file validator uses the inspector validation pipeline");
        Check(inspection.Report.IsValid() && inspection.StructuralHashVerified &&
                  inspection.LogicalHashVerified && inspection.Compatibility == VfstampCompatibility::Compatible &&
                  inspection.Format == "VFSTAMP" && inspection.Endianness == "little-endian" &&
                  inspection.Chunks.size() == 4U && inspection.PaletteCount == 2U &&
                  inspection.Palette.size() == 2U && inspection.Chunks[0].CrcVerified &&
                  inspection.Chunks[0].DeclaredCrc32 == inspection.Chunks[0].ComputedCrc32 &&
                  inspection.StructuralHash != 0U && inspection.LogicalHash != 0U &&
                  inspection.VoxelCount == 2U && inspection.Transform.IsUnitScale(),
              "complete valid inspection");

        const StampValidationReport mismatch = ValidateStampForWrite(MakeStamp("wrong"));
        Check(!mismatch.IsValid() && mismatch.Diagnostics.front().ChunkId == VfstampChunkHash,
              "content-hash diagnostic is structured");
        auto badMagic = write.Bytes;
        badMagic[0] = std::byte{0U};
        const VfstampInspection invalid = InspectVfstampBytes(badMagic);
        Check(!invalid.Report.IsValid() && invalid.Report.Diagnostics.front().Category ==
                  StampDiagnosticCategory::Container &&
                  !invalid.Report.Diagnostics.front().Expected.empty() &&
                  !invalid.Report.Diagnostics.front().Actual.empty() &&
                  !invalid.Report.Diagnostics.front().Explanation.empty(),
              "invalid container inspection returns a safe structured error");
        Check(ValidateVfstampBytes(badMagic) == invalid.Report,
              "file validator reports the same invalid-magic diagnostics");
        auto badCrc = write.Bytes;
        badCrc.back() ^= std::byte{1U};
        const VfstampInspection crcInspection = InspectVfstampBytes(badCrc);
        Check(!crcInspection.Report.IsValid() &&
                  crcInspection.Report.Diagnostics.front().Category == StampDiagnosticCategory::Integrity &&
                  crcInspection.Report.Diagnostics.front().ChunkId == VfstampChunkHash &&
                  crcInspection.Report.Diagnostics.front().Actual == "ChecksumMismatch",
              "CRC diagnostic identifies HASH chunk with a stable error name");
        Check(ValidateVfstampBytes(badCrc) == crcInspection.Report,
              "file validator reports the same CRC diagnostics");
        auto incompatibleMajor = write.Bytes;
        incompatibleMajor[VfstampHeaderMajorVersionOffset] = std::byte{2U};
        const VfstampInspection majorInspection = InspectVfstampBytes(incompatibleMajor);
        Check(majorInspection.MajorVersion == 2U &&
                  majorInspection.Compatibility == VfstampCompatibility::IncompatibleMajor &&
                  majorInspection.Report.Diagnostics.front().Category ==
                      StampDiagnosticCategory::Compatibility &&
                  majorInspection.Report.Diagnostics.front().Expected == "1" &&
                  majorInspection.Report.Diagnostics.front().Actual == "2",
              "major compatibility diagnostic");
        auto structuralMismatch = write.Bytes;
        structuralMismatch[VfstampHeaderMinorVersionOffset] = std::byte{1U};
        const VfstampInspection structuralInspection = InspectVfstampBytes(structuralMismatch);
        Check(!structuralInspection.Report.IsValid() &&
                  structuralInspection.Report.Diagnostics.front().Actual == "StructuralHashMismatch" &&
                  structuralInspection.Report.Diagnostics.front().ChunkId == VfstampChunkHash,
              "structural hash diagnostic identifies HASH");
        auto logicalMismatch = write.Bytes;
        const std::uint64_t hashOffset = ReadU64(
            logicalMismatch, VfstampHeaderSize + 3U * VfstampDirectoryEntrySize +
                                 VfstampDirectoryEntryPayloadOffset);
        logicalMismatch[static_cast<std::size_t>(hashOffset) + VfstampStructuralHashSize] ^=
            std::byte{1U};
        RecomputeV1Hashes(logicalMismatch);
        const VfstampInspection logicalInspection = InspectVfstampBytes(logicalMismatch);
        Check(!logicalInspection.Report.IsValid() &&
                  logicalInspection.Report.Diagnostics.front().Actual == "LogicalHashMismatch",
              "logical hash diagnostic reaches decoder");
        auto reorderedCore = write.Bytes;
        const std::size_t firstEntry = VfstampHeaderSize;
        const std::size_t secondEntry = firstEntry + VfstampDirectoryEntrySize;
        for (std::size_t index = 0U; index < VfstampDirectoryEntrySize; ++index)
        {
            std::swap(reorderedCore[firstEntry + index], reorderedCore[secondEntry + index]);
        }
        RecomputeV1Hashes(reorderedCore);
        const VfstampInspection reorderedInspection = InspectVfstampBytes(reorderedCore);
        Check(!reorderedInspection.Report.IsValid() &&
                  reorderedInspection.Report.Diagnostics.back().Category ==
                      StampDiagnosticCategory::Canonical,
              "reordered core reaches canonical-order inspector diagnostic");
        Check(ValidateVfstampBytes(reorderedCore) == reorderedInspection.Report,
              "file validator reports the same canonical-order diagnostics");
        const std::vector<std::byte> unknownOptional = AddUnknownOptionalChunk(write.Bytes);
        const VfstampInspection extensionInspection = InspectVfstampBytes(unknownOptional);
        const auto extensionWarning = std::find_if(
            extensionInspection.Report.Diagnostics.begin(), extensionInspection.Report.Diagnostics.end(),
            [](const StampDiagnostic& diagnostic) {
                return diagnostic.Severity == StampDiagnosticSeverity::Warning &&
                       diagnostic.ChunkId == MakeVfstampChunkId('U', 'N', 'K', 'N');
            });
        Check(extensionInspection.Report.IsValid() && extensionInspection.Chunks.size() == 5U &&
                  extensionWarning != extensionInspection.Report.Diagnostics.end(),
              "unknown optional extension is preserved and warned");
        Check(ValidateVfstampBytes(unknownOptional) == extensionInspection.Report,
              "file validator reports the same extension warning");
        auto unknownRequired = unknownOptional;
        WriteU32(unknownRequired, VfstampHeaderSize + 4U * VfstampDirectoryEntrySize +
                                      VfstampDirectoryEntryFlagsOffset,
                 VfstampChunkFlagRequired);
        const VfstampInspection unknownRequiredInspection = InspectVfstampBytes(unknownRequired);
        Check(!unknownRequiredInspection.Report.IsValid() &&
                  unknownRequiredInspection.Report.Diagnostics.front().Actual == "UnknownRequiredChunk",
              "unknown required extension is rejected");
        auto newerMinor = write.Bytes;
        newerMinor[VfstampHeaderMinorVersionOffset] = std::byte{1U};
        RecomputeV1Hashes(newerMinor);
        const VfstampInspection newerMinorInspection = InspectVfstampBytes(newerMinor);
        Check(newerMinorInspection.Compatibility == VfstampCompatibility::NewerMinorCompatible &&
                  newerMinorInspection.Report.HasWarnings() &&
                  newerMinorInspection.Report.Diagnostics.front().Category ==
                      StampDiagnosticCategory::Compatibility,
              "newer minor is compatible with a named compatibility warning");
        StampResourceLimits soft = DefaultStampResourceLimits();
        soft.SoftVoxelCount = 1U;
        soft.HardVoxelCount = 2U;
        Check(ValidateStamp(stamp, soft).HasWarnings(), "soft resource diagnostic");
        StampResourceLimits softFile = DefaultStampResourceLimits();
        softFile.SoftFileBytes = 1U;
        softFile.HardFileBytes = write.Bytes.size();
        const VfstampInspection softInspection = InspectVfstampBytes(write.Bytes, softFile);
        Check(softInspection.Report.HasWarnings() &&
                  softInspection.Report.Diagnostics.front().Category == StampDiagnosticCategory::Resource,
              "soft reader limit is categorized as resource warning");
        StampResourceLimits hardFile = softFile;
        hardFile.HardFileBytes = 1U;
        const VfstampInspection hardInspection = InspectVfstampBytes(write.Bytes, hardFile);
        Check(!hardInspection.Report.IsValid() &&
                  hardInspection.Report.Diagnostics.front().Category == StampDiagnosticCategory::Resource,
              "hard reader limit is categorized as resource error");
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            const VfstampInspection repeated = InspectVfstampBytes(write.Bytes);
            Check(repeated.Report.Diagnostics == inspection.Report.Diagnostics &&
                      repeated.Chunks == inspection.Chunks,
                      "inspection is deterministic across repeated reads");
            const VfstampInspection repeatedInvalid = InspectVfstampBytes(badMagic);
            Check(repeatedInvalid.Report == invalid.Report,
                  "invalid inspection is deterministic across repeated reads");
        }
        std::cout << "Vfstamp validation tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
