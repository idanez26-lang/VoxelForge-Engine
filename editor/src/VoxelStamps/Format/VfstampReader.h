#pragma once

#include "VoxelStamps/Format/VfstampFormat.h"
#include "VoxelStamps/StampResourceLimits.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class VfstampReadError
{
    None,
    EmptyInput,
    FileTooSmall,
    FileLimitExceeded,
    InvalidMagic,
    UnsupportedMajorVersion,
    InvalidHeaderFlags,
    InvalidReservedField,
    InvalidChunkCount,
    DirectorySizeOverflow,
    InvalidDirectoryBounds,
    InvalidDirectoryEntry,
    InvalidChunkFlags,
    InvalidChunkBounds,
    ChunkOverlapsContainer,
    ChunkOverlap,
    UnreferencedBytes,
    DuplicateRequiredChunk,
    MissingRequiredChunk,
    UnknownRequiredChunk,
    DecodedSizeOverflow,
    DecodedLimitExceeded,
    ChecksumMismatch,
    StructuralHashMismatch,
    AllocationFailure
};

enum class VfstampReadWarning
{
    NewerMinorVersion,
    SoftResourceLimitExceeded
};

struct VfstampReadResult final
{
    VfstampReadError Error = VfstampReadError::None;
    std::uint32_t ChunkId = 0U;
    std::string_view Message{"Vfstamp container is valid."};
    std::vector<VfstampReadWarning> Warnings;
    std::optional<VfstampContainer> Container;

    [[nodiscard]] bool IsSuccess() const noexcept
    {
        return Error == VfstampReadError::None && Container.has_value();
    }
};

// Parses an in-memory V1 container only.  It performs all range, overlap,
// resource, checksum and structural-hash validation before copying any payload.
// On every error Container is empty; callers never receive partial output.
[[nodiscard]] VfstampReadResult ReadVfstampBytes(
    std::span<const std::byte> bytes,
    const StampResourceLimits& limits = DefaultStampResourceLimits()) noexcept;

} // namespace VoxelForge::Editor::Stamps
