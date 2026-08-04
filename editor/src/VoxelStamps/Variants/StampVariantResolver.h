#pragma once

#include "VoxelStamps/Variants/StampVariantGroup.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class StampVariantResolutionError : std::uint8_t
{
    None,
    MissingFixedSelection,
    FixedVariantNotFound,
    FixedVariantDisabled,
    FixedVariantUnavailable,
    NoValidVariants,
    UnsupportedSelectionMode
};

[[nodiscard]] constexpr std::string_view StampVariantResolutionErrorMessage(
    const StampVariantResolutionError error) noexcept
{
    switch (error)
    {
    case StampVariantResolutionError::None:
        return "Stamp Variant resolved successfully.";
    case StampVariantResolutionError::MissingFixedSelection:
        return "Fixed mode requires an explicitly selected variant UUID.";
    case StampVariantResolutionError::FixedVariantNotFound:
        return "The explicitly selected fixed variant is not part of the group.";
    case StampVariantResolutionError::FixedVariantDisabled:
        return "The explicitly selected fixed variant is disabled.";
    case StampVariantResolutionError::FixedVariantUnavailable:
        return "The explicitly selected fixed variant source is unavailable.";
    case StampVariantResolutionError::NoValidVariants:
        return "The group contains no valid variant for its selection mode.";
    case StampVariantResolutionError::UnsupportedSelectionMode:
        return "The group selection mode is not supported.";
    }
    return "Unknown Stamp Variant resolution error.";
}

enum class StampVariantSkipReason : std::uint8_t
{
    Disabled,
    MissingSource,
    ContentHashMismatch,
    InvalidWeight
};

struct StampVariantResolutionDiagnostic final
{
    Core::UUID VariantId{0U};
    StampVariantSkipReason Reason = StampVariantSkipReason::Disabled;

    [[nodiscard]] bool operator==(
        const StampVariantResolutionDiagnostic&) const noexcept = default;
};

struct ResolvedStampVariant final
{
    Core::UUID VariantId{0U};
    StampAssetReference Stamp;
    std::uint32_t DisplayOrder = 0U;

    [[nodiscard]] bool operator==(const ResolvedStampVariant&) const noexcept = default;
};

struct StampVariantResolutionReport final
{
    StampVariantResolutionError Error = StampVariantResolutionError::None;
    std::string_view Message{
        StampVariantResolutionErrorMessage(StampVariantResolutionError::None)};
    std::optional<ResolvedStampVariant> Resolved;
    std::vector<StampVariantResolutionDiagnostic> Ignored;
    std::uint64_t SelectionSeed = 0U;
    std::uint64_t PlacementOrdinal = 0U;
    std::size_t EligibleVariantCount = 0U;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == StampVariantResolutionError::None &&
            Resolved.has_value();
    }
};

/// Resolves one exact Stamp UUID without reading a repository or mutating a
/// placement session.  The same group UUID, session seed, ordinal, and group
/// revision always produce the same report.
[[nodiscard]] StampVariantResolutionReport ResolveVariant(
    const StampVariantGroup& group,
    std::uint64_t sessionSeed,
    std::uint64_t placementOrdinal);

/// Produces the next stable session seed for an explicit Renew Seed action.
/// It performs no hidden renewal and never returns the current value.
[[nodiscard]] std::uint64_t RenewSessionSeed(
    std::uint64_t currentSeed) noexcept;

} // namespace VoxelForge::Editor::Stamps
