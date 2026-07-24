#include "VoxelStamps/Validation/StampValidationService.h"

#include <algorithm>
#include <array>

namespace VoxelForge::Editor::Stamps
{
namespace
{

void Add(StampValidationReport& report, const StampDiagnosticCategory category,
         const StampDiagnosticSeverity severity, const std::uint32_t chunk,
         std::string expected, std::string actual, std::string explanation)
{
    report.Diagnostics.push_back({category, severity, chunk, std::move(expected),
                                  std::move(actual), std::move(explanation)});
}

StampDiagnosticCategory CategoryForReadError(const VfstampReadError error) noexcept
{
    switch (error)
    {
    case VfstampReadError::ChecksumMismatch:
    case VfstampReadError::StructuralHashMismatch:
        return StampDiagnosticCategory::Integrity;
    case VfstampReadError::FileLimitExceeded:
    case VfstampReadError::InvalidChunkCount:
    case VfstampReadError::DecodedSizeOverflow:
    case VfstampReadError::DecodedLimitExceeded:
        return StampDiagnosticCategory::Resource;
    case VfstampReadError::UnsupportedMajorVersion:
        return StampDiagnosticCategory::Compatibility;
    default:
        return StampDiagnosticCategory::Container;
    }
}

std::string ReadErrorName(const VfstampReadError error)
{
    switch (error)
    {
    case VfstampReadError::EmptyInput: return "EmptyInput";
    case VfstampReadError::FileTooSmall: return "FileTooSmall";
    case VfstampReadError::FileLimitExceeded: return "FileLimitExceeded";
    case VfstampReadError::InvalidMagic: return "InvalidMagic";
    case VfstampReadError::UnsupportedMajorVersion: return "UnsupportedMajorVersion";
    case VfstampReadError::InvalidHeaderFlags: return "InvalidHeaderFlags";
    case VfstampReadError::InvalidReservedField: return "InvalidReservedField";
    case VfstampReadError::InvalidChunkCount: return "InvalidChunkCount";
    case VfstampReadError::DirectorySizeOverflow: return "DirectorySizeOverflow";
    case VfstampReadError::InvalidDirectoryBounds: return "InvalidDirectoryBounds";
    case VfstampReadError::InvalidDirectoryEntry: return "InvalidDirectoryEntry";
    case VfstampReadError::InvalidChunkFlags: return "InvalidChunkFlags";
    case VfstampReadError::InvalidChunkBounds: return "InvalidChunkBounds";
    case VfstampReadError::ChunkOverlapsContainer: return "ChunkOverlapsContainer";
    case VfstampReadError::ChunkOverlap: return "ChunkOverlap";
    case VfstampReadError::UnreferencedBytes: return "UnreferencedBytes";
    case VfstampReadError::DuplicateRequiredChunk: return "DuplicateRequiredChunk";
    case VfstampReadError::MissingRequiredChunk: return "MissingRequiredChunk";
    case VfstampReadError::UnknownRequiredChunk: return "UnknownRequiredChunk";
    case VfstampReadError::DecodedSizeOverflow: return "DecodedSizeOverflow";
    case VfstampReadError::DecodedLimitExceeded: return "DecodedLimitExceeded";
    case VfstampReadError::ChecksumMismatch: return "ChecksumMismatch";
    case VfstampReadError::StructuralHashMismatch: return "StructuralHashMismatch";
    case VfstampReadError::AllocationFailure: return "AllocationFailure";
    case VfstampReadError::None: return "None";
    }
    return "UnknownReadError";
}

std::string DecodeErrorName(const VfstampDecodeError error)
{
    switch (error)
    {
    case VfstampDecodeError::ContainerError: return "ContainerError";
    case VfstampDecodeError::MissingChunk: return "MissingChunk";
    case VfstampDecodeError::InvalidHashPayload: return "InvalidHashPayload";
    case VfstampDecodeError::LogicalHashMismatch: return "LogicalHashMismatch";
    case VfstampDecodeError::InvalidManifest: return "InvalidManifest";
    case VfstampDecodeError::InvalidPalette: return "InvalidPalette";
    case VfstampDecodeError::InvalidVoxelData: return "InvalidVoxelData";
    case VfstampDecodeError::ResourceLimitExceeded: return "ResourceLimitExceeded";
    case VfstampDecodeError::DomainValidationFailed: return "DomainValidationFailed";
    case VfstampDecodeError::AllocationFailure: return "AllocationFailure";
    case VfstampDecodeError::None: return "None";
    }
    return "UnknownDecodeError";
}

std::string ReadWarningName(const VfstampReadWarning warning)
{
    switch (warning)
    {
    case VfstampReadWarning::NewerMinorVersion: return "NewerMinorVersion";
    case VfstampReadWarning::SoftResourceLimitExceeded: return "SoftResourceLimitExceeded";
    }
    return "UnknownReadWarning";
}

std::uint16_t ReadLe16(const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset])) |
           static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1U]) << 8U);
}

std::uint32_t ReadLe32(const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    std::uint32_t result = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
        result |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    return result;
}

} // namespace

bool StampValidationReport::IsValid() const noexcept
{
    return std::none_of(Diagnostics.begin(), Diagnostics.end(), [](const StampDiagnostic& diagnostic) {
        return diagnostic.Severity == StampDiagnosticSeverity::Error;
    });
}

bool StampValidationReport::HasWarnings() const noexcept
{
    return std::any_of(Diagnostics.begin(), Diagnostics.end(), [](const StampDiagnostic& diagnostic) {
        return diagnostic.Severity == StampDiagnosticSeverity::Warning;
    });
}

StampValidationReport ValidateStamp(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits)
{
    StampValidationReport report;
    const StampValidationResult validation = VoxelStamp::Validate(
        stamp.Identity(), stamp.Bounds(), stamp.Pivot(), stamp.Transform(), stamp.Palette(),
        stamp.Voxels(), limits);
    if (!validation.IsValid())
    {
        Add(report, StampDiagnosticCategory::Domain, StampDiagnosticSeverity::Error, 0U,
            "valid VoxelStamp invariants", std::to_string(static_cast<int>(validation.Error)),
            std::string(validation.Message));
    }
    const StampSizeEstimate estimate = EstimateDecodedStampBytes(
        0U, stamp.Voxels().size(), sizeof(StampVoxel), stamp.Palette().size(),
        sizeof(StampPaletteEntry));
    const StampLimitEvaluation limitsEvaluation = EvaluateStampLimits(
        {.VoxelCount = stamp.Voxels().size(),
         .LargestAxisLength = std::max({stamp.Bounds().Dimensions.X,
                                        stamp.Bounds().Dimensions.Y,
                                        stamp.Bounds().Dimensions.Z}),
         .DecodedBytes = estimate.DecodedBytes,
         .ArithmeticOverflow = estimate.ArithmeticOverflow},
        limits);
    if (limitsEvaluation.HasWarning())
    {
        Add(report, StampDiagnosticCategory::Resource, StampDiagnosticSeverity::Warning, 0U,
            std::to_string(limitsEvaluation.LimitValue),
            std::to_string(limitsEvaluation.ActualValue), std::string(limitsEvaluation.Message));
    }
    return report;
}

StampValidationReport ValidateStampForWrite(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits)
{
    StampValidationReport report = ValidateStamp(stamp, limits);
    if (!report.IsValid())
    {
        return report;
    }
    const std::string logicalHash = CalculateVfstampLogicalContentHash(stamp);
    if (!stamp.Identity().ContentHash.empty() && stamp.Identity().ContentHash != logicalHash)
    {
        Add(report, StampDiagnosticCategory::Integrity, StampDiagnosticSeverity::Error,
            VfstampChunkHash, logicalHash, stamp.Identity().ContentHash,
            "Identity content hash must match the canonical logical hash before writing.");
    }
    return report;
}

StampValidationReport ValidateStampForPlacement(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits)
{
    // Placement-specific document collision and palette checks intentionally
    // belong to STAMP-13; STAMP-04 validates portable stamp semantics only.
    return ValidateStamp(stamp, limits);
}

StampValidationReport ValidateVfstampBytes(
    const std::span<const std::byte> bytes,
    const StampResourceLimits& limits)
{
    return InspectVfstampBytes(bytes, limits).Report;
}

VfstampInspection InspectVfstampBytes(
    const std::span<const std::byte> bytes,
    const StampResourceLimits& limits)
{
    VfstampInspection inspection;
    if (bytes.size() >= VfstampHeaderSize)
    {
        inspection.MajorVersion = ReadLe16(bytes, VfstampHeaderMajorVersionOffset);
        inspection.MinorVersion = ReadLe16(bytes, VfstampHeaderMinorVersionOffset);
    }
    const VfstampReadResult read = ReadVfstampBytes(bytes, limits);
    if (!read.IsSuccess())
    {
        std::string expected{"structurally valid V1 container"};
        std::string actual{ReadErrorName(read.Error)};
        if (read.Error == VfstampReadError::UnsupportedMajorVersion)
        {
            expected = std::to_string(VfstampFormatMajorVersion);
            actual = std::to_string(inspection.MajorVersion);
        }
        else if (read.Error == VfstampReadError::InvalidHeaderFlags &&
                 bytes.size() >= VfstampHeaderSize)
        {
            expected = "0";
            actual = std::to_string(ReadLe32(bytes, VfstampHeaderFlagsOffset));
        }
        Add(inspection.Report, CategoryForReadError(read.Error), StampDiagnosticSeverity::Error,
            read.ChunkId, std::move(expected), std::move(actual), std::string(read.Message));
        if (read.Error == VfstampReadError::UnsupportedMajorVersion)
        {
            inspection.Compatibility = VfstampCompatibility::IncompatibleMajor;
        }
        return inspection;
    }

    inspection.MajorVersion = read.Container->MajorVersion;
    inspection.MinorVersion = read.Container->MinorVersion;
    inspection.Compatibility = read.Container->MinorVersion > VfstampFormatMinorVersion
                                   ? VfstampCompatibility::NewerMinorCompatible
                                   : VfstampCompatibility::Compatible;
    inspection.StructuralHashVerified = true;
    for (const VfstampReadWarning warning : read.Warnings)
    {
        const StampDiagnosticCategory category =
            warning == VfstampReadWarning::SoftResourceLimitExceeded
                ? StampDiagnosticCategory::Resource
                : StampDiagnosticCategory::Compatibility;
        Add(inspection.Report, category, StampDiagnosticSeverity::Warning, 0U,
            "current V1 limits/version", ReadWarningName(warning),
            "Container was accepted with a warning.");
    }
    for (const VfstampChunk& chunk : read.Container->Chunks)
    {
        const std::uint32_t computedCrc = ComputeVfstampCrc32(chunk.Payload);
        inspection.Chunks.push_back({.Id = chunk.Id,
                                     .Flags = chunk.Flags,
                                     .PayloadBytes = chunk.Payload.size(),
                                     .DeclaredCrc32 = chunk.Crc32,
                                     .ComputedCrc32 = computedCrc,
                                     .CrcVerified = computedCrc == chunk.Crc32});
    }
    for (const VfstampChunkInspection& chunk : inspection.Chunks)
    {
        const bool known = chunk.Id == VfstampChunkManf || chunk.Id == VfstampChunkPal0 ||
                           chunk.Id == VfstampChunkVox0 || chunk.Id == VfstampChunkHash ||
                           chunk.Id == VfstampChunkThmb || chunk.Id == VfstampChunkAnch ||
                           chunk.Id == VfstampChunkExtn;
        if (!known)
        {
            Add(inspection.Report, StampDiagnosticCategory::Compatibility,
                StampDiagnosticSeverity::Warning, chunk.Id,
                "known V1 chunk or registered future handler", "unknown optional chunk",
                "Opaque optional extension was preserved but not interpreted by V1.");
        }
    }

    const VfstampDecodeResult decoded = DecodeVfstampContainer(*read.Container, limits);
    if (!decoded.IsSuccess())
    {
        Add(inspection.Report, StampDiagnosticCategory::Domain, StampDiagnosticSeverity::Error,
            0U, "decodable V1 semantic payloads", DecodeErrorName(decoded.Error),
            std::string(decoded.Message));
        return inspection;
    }
    inspection.LogicalHashVerified = true;
    const VfstampChunk* hashChunk = nullptr;
    for (const VfstampChunk& chunk : read.Container->Chunks)
    {
        if (chunk.Id == VfstampChunkHash)
        {
            hashChunk = &chunk;
            break;
        }
    }
    if (hashChunk != nullptr && hashChunk->Payload.size() >= VfstampStructuralHashSize)
    {
        const auto readHash = [](const std::span<const std::byte> payload,
                                 const std::size_t offset) noexcept {
            std::uint64_t value = 0U;
            for (std::size_t index = 0U; index < 8U; ++index)
                value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(payload[offset + index])) << (index * 8U);
            return value;
        };
        inspection.StructuralHash = readHash(hashChunk->Payload, 0U);
        inspection.LogicalHash = readHash(hashChunk->Payload, VfstampStructuralHashSize);
    }
    inspection.PaletteCount = static_cast<std::uint32_t>(decoded.Stamp->Palette().size());
    inspection.Palette.assign(decoded.Stamp->Palette().begin(), decoded.Stamp->Palette().end());
    inspection.VoxelCount = decoded.Stamp->Voxels().size();
    inspection.Bounds = decoded.Stamp->Bounds();
    inspection.Pivot = decoded.Stamp->Pivot();
    inspection.Transform = decoded.Stamp->Transform();

    const std::array<std::uint32_t, 4U> core{
        VfstampChunkManf, VfstampChunkPal0, VfstampChunkVox0, VfstampChunkHash};
    if (inspection.Chunks.size() < core.size() ||
        !std::equal(core.begin(), core.end(), inspection.Chunks.begin(),
                    [](const std::uint32_t id, const VfstampChunkInspection& chunk) {
                        return id == chunk.Id;
                    }))
    {
        Add(inspection.Report, StampDiagnosticCategory::Canonical,
            StampDiagnosticSeverity::Error, 0U, "MANF/PAL0/VOX0/HASH canonical core order",
            "different order", "V1 writers must emit core chunks in canonical order.");
    }
    return inspection;
}

} // namespace VoxelForge::Editor::Stamps
