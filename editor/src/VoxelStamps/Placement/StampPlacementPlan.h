#pragma once

#include "VoxelStamps/Palette/PaletteMappingEngine.h"
#include "VoxelStamps/StampResourceLimits.h"
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

/// Exact grid mirrors supported by STAMP-17. The mode is resolved before the
/// quarter rotation and is part of the immutable transform/cache identity.
enum class StampPlacementMirrorMode : std::uint8_t
{
    None,
    X,
    Z,
    XZ
};

/// STAMP-15 supports exact quarter turns around the vertical Y axis only.
/// Keeping the axis explicit avoids changing the transform contract when
/// additional exact grid axes are introduced later.
enum class StampPlacementRotationAxis : std::uint8_t
{
    VerticalY
};

struct StampPlacementTransform final
{
    StampFixedPoint TargetPivot{};
    StampPlacementRotationAxis RotationAxis =
        StampPlacementRotationAxis::VerticalY;
    std::uint8_t QuarterTurns = 0U;
    StampPlacementMirrorMode Mirror = StampPlacementMirrorMode::None;

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

/// Exact Smart Variant facts resolved before planning.  They participate in
/// plan/cache identity and are copied unchanged into history on commit.
struct StampPlacementVariantIdentity final
{
    Core::UUID GroupId{0U};
    std::uint64_t GroupRevision = 0U;
    Core::UUID VariantId{0U};
    Core::UUID StampId{0U};
    std::string ExpectedContentHash;
    std::uint64_t PlacementSessionSeed = 0U;
    std::uint64_t SelectionSeed = 0U;
    std::uint64_t PlacementOrdinal = 0U;

    [[nodiscard]] bool operator==(
        const StampPlacementVariantIdentity&) const noexcept = default;
};

struct StampPlacementCacheKey final
{
    StampIdentity Stamp{};
    std::optional<StampPlacementVariantIdentity> Variant;
    StampDocumentIdentity Document{};
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::size_t TargetSubModel = 0U;
    StampPlacementTransform Transform{};
    StampCollisionPolicy CollisionPolicy = StampCollisionPolicy::Overwrite;
    std::size_t PaletteCapacity = 256U;
    std::uint8_t ReservedDocumentPaletteIndex = 0U;
    StampResourceLimits ResourceLimits{};

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
    InvalidVariantIdentity,
    UnsupportedRotation,
    UnsupportedMirror,
    UnsupportedCollisionPolicy,
    PaletteMappingFailed,
    PositionNotRepresentable,
    OutOfBounds,
    CollisionRejected,
    SoftResourceLimitExceeded,
    HardResourceLimitExceeded,
    InvalidResourceLimits,
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
    case StampPlacementDiagnosticCode::InvalidVariantIdentity:
        return "Resolved Stamp Variant identity does not match the active Stamp.";
    case StampPlacementDiagnosticCode::UnsupportedRotation:
        return "Stamp rotation must be an exact vertical quarter turn (0, 90, 180, or 270 degrees).";
    case StampPlacementDiagnosticCode::UnsupportedMirror:
        return "Stamp mirror must be None, X, Z, or XZ.";
    case StampPlacementDiagnosticCode::UnsupportedCollisionPolicy:
        return "The selected Stamp collision policy is unknown.";
    case StampPlacementDiagnosticCode::PaletteMappingFailed:
        return "The Stamp palette cannot be mapped into the active document.";
    case StampPlacementDiagnosticCode::PositionNotRepresentable:
        return "A transformed Stamp position is not exactly representable on the voxel grid.";
    case StampPlacementDiagnosticCode::OutOfBounds:
        return "One or more Stamp voxels lie outside the target sub-model.";
    case StampPlacementDiagnosticCode::CollisionRejected:
        return "The selected collision policy rejects an occupied destination.";
    case StampPlacementDiagnosticCode::SoftResourceLimitExceeded:
        return "Stamp placement exceeds a soft resource limit but remains allowed.";
    case StampPlacementDiagnosticCode::HardResourceLimitExceeded:
        return "Stamp placement exceeds a hard resource limit.";
    case StampPlacementDiagnosticCode::InvalidResourceLimits:
        return "Stamp placement resource limits are invalid.";
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
    std::size_t SkippedVoxelCount = 0U;
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
    bool Skipped = false;
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
    std::optional<StampPlacementVariantIdentity> Variant;
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
    StampLimitEvaluation ResourceLimitEvaluation{};
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
