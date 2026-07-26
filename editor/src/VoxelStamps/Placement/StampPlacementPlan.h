#pragma once

#include "VoxelStamps/Palette/PaletteMappingEngine.h"
#include "VoxelStamps/StampTypes.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class StampCollisionPolicy : std::uint8_t
{
    Overwrite,
    Reject,
    SkipOccupied
};

struct StampPlacementMirror final
{
    bool X = false;
    bool Y = false;
    bool Z = false;

    [[nodiscard]] bool operator==(const StampPlacementMirror&) const noexcept =
        default;
};

struct StampPlacementTransform final
{
    StampFixedPoint TargetPivot{};
    std::uint8_t QuarterTurns = 0U;
    StampPlacementMirror Mirror{};

    [[nodiscard]] bool operator==(
        const StampPlacementTransform&) const noexcept = default;
};

struct StampDocumentIdentity final
{
    // Process-local identity. It is never serialized and is paired with the
    // document generation so a reused address cannot validate a stale plan.
    std::uintptr_t InstanceToken = 0U;
    std::string SourcePath;
    std::string AssetId;

    [[nodiscard]] bool operator==(
        const StampDocumentIdentity&) const noexcept = default;
};

struct StampPlacementCacheKey final
{
    StampIdentity Stamp{};
    StampDocumentIdentity Document{};
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::size_t TargetSubModel = 0U;
    StampPlacementTransform Transform{};
    StampCollisionPolicy CollisionPolicy = StampCollisionPolicy::Overwrite;

    [[nodiscard]] bool operator==(
        const StampPlacementCacheKey&) const noexcept = default;
};

enum class StampPlacementDiagnosticSeverity : std::uint8_t
{
    Information,
    Warning,
    Error
};

enum class StampPlacementDiagnosticCode : std::uint8_t
{
    None,
    MissingStamp,
    MissingDocument,
    InvalidDocumentGeneration,
    InvalidSubModel,
    UnsupportedRotation,
    UnsupportedMirror,
    UnsupportedCollisionPolicy,
    PaletteMappingFailed,
    PositionNotRepresentable,
    OutOfBounds,
    CollisionRejected,
    NoChanges,
    AllocationFailure
};

[[nodiscard]] constexpr std::string_view StampPlacementDiagnosticMessage(
    const StampPlacementDiagnosticCode code) noexcept
{
    switch (code)
    {
    case StampPlacementDiagnosticCode::None:
        return "Stamp placement plan is valid.";
    case StampPlacementDiagnosticCode::MissingStamp:
        return "A Stamp is required to build a placement plan.";
    case StampPlacementDiagnosticCode::MissingDocument:
        return "An active voxel document is required to build a placement plan.";
    case StampPlacementDiagnosticCode::InvalidDocumentGeneration:
        return "The active voxel document generation is invalid.";
    case StampPlacementDiagnosticCode::InvalidSubModel:
        return "The target voxel sub-model does not exist.";
    case StampPlacementDiagnosticCode::UnsupportedRotation:
        return "Stamp rotation is reserved but is not supported by V1 planning yet.";
    case StampPlacementDiagnosticCode::UnsupportedMirror:
        return "Stamp mirror is reserved but is not supported by V1 planning yet.";
    case StampPlacementDiagnosticCode::UnsupportedCollisionPolicy:
        return "Only the non-blocking overwrite collision policy is supported.";
    case StampPlacementDiagnosticCode::PaletteMappingFailed:
        return "The Stamp palette cannot be mapped into the active document.";
    case StampPlacementDiagnosticCode::PositionNotRepresentable:
        return "A transformed Stamp position is not exactly representable on the voxel grid.";
    case StampPlacementDiagnosticCode::OutOfBounds:
        return "One or more Stamp voxels lie outside the target sub-model.";
    case StampPlacementDiagnosticCode::CollisionRejected:
        return "The selected collision policy rejects an occupied destination.";
    case StampPlacementDiagnosticCode::NoChanges:
        return "The planned Stamp already matches the target document.";
    case StampPlacementDiagnosticCode::AllocationFailure:
        return "Stamp placement planning ran out of memory.";
    }

    return "Unknown Stamp placement planning diagnostic.";
}

struct StampPlacementDiagnostic final
{
    StampPlacementDiagnosticCode Code = StampPlacementDiagnosticCode::None;
    StampPlacementDiagnosticSeverity Severity =
        StampPlacementDiagnosticSeverity::Information;
    std::optional<std::size_t> SourceOrdinal;

    [[nodiscard]] std::string_view Message() const noexcept
    {
        return StampPlacementDiagnosticMessage(Code);
    }

    [[nodiscard]] bool operator==(
        const StampPlacementDiagnostic&) const noexcept = default;
};

struct StampPlacementBounds final
{
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};
    bool Valid = false;

    [[nodiscard]] bool operator==(
        const StampPlacementBounds&) const noexcept = default;
};

struct StampPlacementStatistics final
{
    std::size_t TotalVoxelCount = 0U;
    std::size_t PlannedVoxelCount = 0U;
    std::size_t ChangedVoxelCount = 0U;
    std::size_t UnchangedVoxelCount = 0U;
    std::size_t OverlapCount = 0U;
    std::size_t OutOfBoundsCount = 0U;
    std::size_t AddedPaletteColorCount = 0U;
    std::size_t ReusedPaletteColorCount = 0U;

    [[nodiscard]] bool operator==(
        const StampPlacementStatistics&) const noexcept = default;
};

struct StampPlannedVoxel final
{
    std::size_t SourceOrdinal = 0U;
    StampLocalPosition LocalPosition{};
    Asset::Voxel::VoxelPosition WorldPosition{};
    std::uint8_t LocalPaletteIndex = 0U;
    std::uint8_t DocumentPaletteIndex = 0U;
    StampColor Color{};
    std::optional<Asset::Voxel::Voxel> ExistingVoxel;
    Asset::Voxel::Voxel FinalVoxel{};
    bool Overlap = false;
    bool OutOfBounds = false;

    [[nodiscard]] bool operator==(
        const StampPlannedVoxel&) const noexcept = default;
};

/// Complete immutable decision record for one Stamp placement. Preview and
/// transaction adapters consume this object and are forbidden from reading the
/// document or recalculating placement policy.
struct StampPlacementPlan final
{
    StampIdentity Stamp{};
    StampDocumentIdentity Document{};
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::size_t TargetSubModel = 0U;
    StampBounds LocalBounds{};
    StampPivot Pivot{};
    StampPlacementTransform Transform{};
    StampCollisionPolicy CollisionPolicy = StampCollisionPolicy::Overwrite;
    Asset::Voxel::VoxelDocumentPaletteSnapshot DocumentPaletteBefore{};
    PaletteMappingPlan PaletteMapping{};
    PaletteMappingStatus PaletteStatus = PaletteMappingStatus::Success;
    StampPlacementBounds WorldBounds{};
    StampPlacementStatistics Statistics{};
    std::vector<StampPlacementDiagnostic> Diagnostics;
    std::vector<StampPlannedVoxel> Voxels;
    StampPlacementCacheKey CacheKey{};
    bool CanCommit = false;

    [[nodiscard]] bool IsCurrent(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t subModelIndex) const noexcept;
    [[nodiscard]] bool HasErrors() const noexcept;
};

[[nodiscard]] StampDocumentIdentity MakeStampDocumentIdentity(
    const Asset::Voxel::VoxelDocument& document);

} // namespace VoxelForge::Editor::Stamps
