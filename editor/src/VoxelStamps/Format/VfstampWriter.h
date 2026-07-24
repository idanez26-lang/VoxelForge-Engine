#pragma once

#include "VoxelStamps/Format/VfstampReader.h"
#include "VoxelStamps/VoxelStamp.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class VfstampWriteError
{
    None,
    InvalidStamp,
    ContentHashMismatch,
    ResourceLimitExceeded,
    ArithmeticOverflow,
    InternalValidationFailed,
    AllocationFailure
};

struct VfstampWriteResult final
{
    VfstampWriteError Error = VfstampWriteError::None;
    std::string_view Message{"Vfstamp bytes were written."};
    // Populated only on success.  This is the canonical hash used when the
    // immutable input identity intentionally carried an empty ContentHash.
    std::string LogicalContentHash;
    std::vector<std::byte> Bytes;

    [[nodiscard]] bool IsSuccess() const noexcept { return Error == VfstampWriteError::None && !Bytes.empty(); }
};

enum class VfstampDecodeError
{
    None,
    ContainerError,
    MissingChunk,
    InvalidHashPayload,
    LogicalHashMismatch,
    InvalidManifest,
    InvalidPalette,
    InvalidVoxelData,
    ResourceLimitExceeded,
    DomainValidationFailed,
    AllocationFailure
};

struct VfstampDecodeResult final
{
    VfstampDecodeError Error = VfstampDecodeError::None;
    std::string_view Message{"Vfstamp domain payloads are valid."};
    std::optional<VoxelStamp> Stamp;

    [[nodiscard]] bool IsSuccess() const noexcept { return Error == VfstampDecodeError::None && Stamp.has_value(); }
};

// Lowercase 16-hex-digit FNV-1a-64 digest of logical V1 domain data only:
// UUID, bounds/dimensions, pivot, fixed unit scale, canonical palette and
// lexicographically sorted voxels. Identity.ContentHash itself is excluded.
[[nodiscard]] std::string CalculateVfstampLogicalContentHash(const VoxelStamp& stamp);

// An empty identity content hash is canonicalized to the computed logical hash.
// A non-empty value must equal it exactly or writing fails without bytes.
[[nodiscard]] VfstampWriteResult WriteVfstampBytes(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits = DefaultStampResourceLimits()) noexcept;

// Decoding is STAMP-03's domain bridge: it consumes a structurally validated
// container and returns only a VoxelStamp created through VoxelStamp::TryCreate.
[[nodiscard]] VfstampDecodeResult DecodeVfstampContainer(
    const VfstampContainer& container,
    const StampResourceLimits& limits = DefaultStampResourceLimits()) noexcept;
[[nodiscard]] VfstampDecodeResult DecodeVfstampBytes(
    std::span<const std::byte> bytes,
    const StampResourceLimits& limits = DefaultStampResourceLimits()) noexcept;

} // namespace VoxelForge::Editor::Stamps
