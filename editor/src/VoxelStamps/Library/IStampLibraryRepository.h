#pragma once

#include "VoxelStamps/VoxelStamp.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class StampLibraryScope
{
    Project,
    User
};

enum class StampLibraryError
{
    None,
    NotConfigured,
    InvalidProjectRoot,
    InvalidReference,
    PathEscapesProjectLibrary,
    SymbolicLinkRejected,
    StaleTransactionFile,
    AssetNotFound,
    AssetNotRegularFile,
    InvalidAsset,
    SerializationFailed,
    IoFailure,
    TransactionFailed,
    RollbackFailed,
    AllocationFailure
};

[[nodiscard]] constexpr std::string_view StampLibraryErrorMessage(
    const StampLibraryError error) noexcept
{
    switch (error)
    {
    case StampLibraryError::None: return "Stamp Library operation completed.";
    case StampLibraryError::NotConfigured: return "Stamp Library has no configured storage root.";
    case StampLibraryError::InvalidProjectRoot: return "Stamp Library storage root is invalid.";
    case StampLibraryError::InvalidReference: return "Stamp reference must be a portable .vfstamp path.";
    case StampLibraryError::PathEscapesProjectLibrary: return "Stamp path escapes the configured Creations directory.";
    case StampLibraryError::SymbolicLinkRejected: return "Symbolic links and reparse-path escapes are not allowed.";
    case StampLibraryError::StaleTransactionFile: return "A stale Stamp transaction file requires manual recovery.";
    case StampLibraryError::AssetNotFound: return "Stamp source asset was not found.";
    case StampLibraryError::AssetNotRegularFile: return "Stamp source asset must be a regular file.";
    case StampLibraryError::InvalidAsset: return "Stamp source asset failed Vfstamp validation.";
    case StampLibraryError::SerializationFailed: return "Stamp could not be serialized for installation.";
    case StampLibraryError::IoFailure: return "Stamp Library filesystem operation failed.";
    case StampLibraryError::TransactionFailed: return "Stamp installation transaction failed without publishing new content.";
    case StampLibraryError::RollbackFailed: return "Stamp installation failed and original content could not be restored.";
    case StampLibraryError::AllocationFailure: return "Project Stamp Library operation ran out of memory.";
    }
    return "Unknown Stamp Library error.";
}

struct StampAssetReference final
{
    Core::UUID Id{0U};
    std::string ContentHash;
    std::filesystem::path RelativePath;
    StampLibraryScope Scope = StampLibraryScope::Project;

    [[nodiscard]] bool operator==(const StampAssetReference&) const noexcept = default;
};

struct StampLibraryAsset final
{
    StampAssetReference Reference;
    std::uintmax_t FileBytes = 0U;

    [[nodiscard]] bool operator==(const StampLibraryAsset&) const noexcept = default;
};

struct StampLibraryDiagnostic final
{
    StampLibraryError Code = StampLibraryError::None;
    std::filesystem::path RelativePath;
    std::string Message;
};

struct StampLibraryResult final
{
    StampLibraryError Error = StampLibraryError::None;
    std::string Message{StampLibraryErrorMessage(StampLibraryError::None)};
    StampAssetReference Reference;
    std::optional<VoxelStamp> Stamp;
    std::vector<StampLibraryAsset> Assets;
    std::vector<StampLibraryDiagnostic> Diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == StampLibraryError::None;
    }
};

struct StampInstallOptions final
{
    std::string PreferredFileStem;
    bool ReplaceExisting = false;
};

class IStampLibraryRepository
{
public:
    virtual ~IStampLibraryRepository() = default;

    [[nodiscard]] virtual StampLibraryResult Install(
        const VoxelStamp& stamp,
        const StampInstallOptions& options = {}) = 0;
    [[nodiscard]] virtual StampLibraryResult Read(
        const StampAssetReference& reference) const = 0;
    [[nodiscard]] virtual StampLibraryResult EnumerateSourceAssets() const = 0;
    [[nodiscard]] virtual StampLibraryResult Remove(
        const StampAssetReference& reference) = 0;
    [[nodiscard]] virtual StampLibraryResult ResolvePortableReference(
        const std::filesystem::path& relativePath) const = 0;
    [[nodiscard]] virtual StampLibraryResult RebuildSourceInventory() const = 0;
};

} // namespace VoxelForge::Editor::Stamps
