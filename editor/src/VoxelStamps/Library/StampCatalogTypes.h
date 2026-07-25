#pragma once

#include "VoxelStamps/Library/IStampLibraryRepository.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

inline constexpr std::uint32_t StampCatalogVersion = 1U;
inline constexpr std::size_t StampCatalogMaximumBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t StampCatalogMaximumEntries = 100000U;

/// Derived, portable facts about a Project Library source asset.  The
/// .vfstamp remains the authority; this record is only an indexable snapshot.
struct StampCatalogEntry final
{
    StampAssetReference Reference;
    std::string FileName;
    std::uintmax_t FileBytes = 0U;
    StampDimensions Dimensions{};
    std::uint64_t VoxelCount = 0U;
    std::uint32_t PaletteCount = 0U;

    [[nodiscard]] bool operator==(const StampCatalogEntry&) const noexcept = default;
};

struct StampCatalog final
{
    std::uint32_t Version = StampCatalogVersion;
    std::vector<StampCatalogEntry> Entries;

    [[nodiscard]] bool operator==(const StampCatalog&) const noexcept = default;
};

enum class StampCatalogError
{
    None,
    NotConfigured,
    InvalidProjectRoot,
    Missing,
    Invalid,
    UnsupportedVersion,
    PathEscapesProjectLibrary,
    SymbolicLinkRejected,
    StaleTransaction,
    IdentityCollision,
    IoFailure,
    TransactionFailed,
    RollbackFailed,
    AllocationFailure,
};

[[nodiscard]] constexpr std::string_view StampCatalogErrorMessage(
    const StampCatalogError error) noexcept
{
    switch (error)
    {
    case StampCatalogError::None: return "Stamp catalogue operation completed.";
    case StampCatalogError::NotConfigured: return "Project Stamp catalogue has no configured project root.";
    case StampCatalogError::InvalidProjectRoot: return "Project root or Assets directory is invalid.";
    case StampCatalogError::Missing: return "Stamp catalogue is missing.";
    case StampCatalogError::Invalid: return "Stamp catalogue JSON is malformed or violates V1 validation.";
    case StampCatalogError::UnsupportedVersion: return "Stamp catalogue version is not supported.";
    case StampCatalogError::PathEscapesProjectLibrary: return "Stamp catalogue path escapes the project Forge Library.";
    case StampCatalogError::SymbolicLinkRejected: return "Symbolic links and reparse-path escapes are not allowed.";
    case StampCatalogError::StaleTransaction: return "A stale Stamp catalogue transaction file requires manual recovery.";
    case StampCatalogError::IdentityCollision: return "Stamp catalogue source assets contain conflicting identities or paths.";
    case StampCatalogError::IoFailure: return "Stamp catalogue filesystem operation failed.";
    case StampCatalogError::TransactionFailed: return "Stamp catalogue publication failed without publishing new content.";
    case StampCatalogError::RollbackFailed: return "Stamp catalogue publication failed and the previous catalogue could not be restored.";
    case StampCatalogError::AllocationFailure: return "Stamp catalogue operation ran out of memory.";
    }
    return "Unknown Stamp catalogue error.";
}

struct StampCatalogDiagnostic final
{
    StampCatalogError Code = StampCatalogError::None;
    std::filesystem::path RelativePath;
    std::string Message;
};

struct StampCatalogResult final
{
    StampCatalogError Error = StampCatalogError::None;
    std::string Message{StampCatalogErrorMessage(StampCatalogError::None)};
    StampCatalog Catalog;
    std::vector<StampCatalogDiagnostic> Diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept { return Error == StampCatalogError::None; }
};

/// All populated fields narrow the result. Text is a case-insensitive ASCII
/// substring over the portable path, filename, UUID and content hash.
struct StampCatalogQuery final
{
    std::optional<Core::UUID> Id;
    std::string ContentHash;
    std::filesystem::path RelativePath;
    std::string Text;
};

} // namespace VoxelForge::Editor::Stamps
