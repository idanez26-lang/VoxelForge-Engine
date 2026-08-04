#include "VoxelStamps/Variants/StampVariantResolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge::Editor::Stamps;

constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t FnvPrime = 1099511628211ULL;

[[nodiscard]] std::uint64_t SplitMix64(std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

void HashU64(std::uint64_t& hash, const std::uint64_t value) noexcept
{
    // Explicit little-endian byte hashing avoids native layout and std::hash.
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
    {
        hash ^= (value >> shift) & 0xffU;
        hash *= FnvPrime;
    }
}

[[nodiscard]] std::uint64_t SelectionSeed(
    const StampVariantGroup& group,
    const std::uint64_t sessionSeed,
    const std::uint64_t placementOrdinal) noexcept
{
    std::uint64_t hash = FnvOffset;
    HashU64(hash, group.Id().Value());
    HashU64(hash, sessionSeed);
    HashU64(hash, placementOrdinal);
    return SplitMix64(hash);
}

[[nodiscard]] bool StableLess(
    const StampVariant* left,
    const StampVariant* right) noexcept
{
    if (left->DisplayOrder != right->DisplayOrder)
    {
        return left->DisplayOrder < right->DisplayOrder;
    }
    return left->Id.Value() < right->Id.Value();
}

[[nodiscard]] std::optional<StampVariantSkipReason> IneligibleReason(
    const StampVariant& variant,
    const bool requireWeight) noexcept
{
    if (!variant.Enabled) return StampVariantSkipReason::Disabled;
    if (variant.SourceState == StampVariantSourceState::Missing)
    {
        return StampVariantSkipReason::MissingSource;
    }
    if (variant.SourceState == StampVariantSourceState::ContentHashMismatch)
    {
        return StampVariantSkipReason::ContentHashMismatch;
    }
    if (requireWeight &&
        (!std::isfinite(variant.Weight) || variant.Weight <= 0.0))
    {
        return StampVariantSkipReason::InvalidWeight;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<const StampVariant*> EligibleVariants(
    const StampVariantGroup& group,
    const bool requireWeight,
    StampVariantResolutionReport& report)
{
    std::vector<const StampVariant*> eligible;
    eligible.reserve(group.Variants().size());
    report.Ignored.reserve(group.Variants().size());
    for (const StampVariant& variant : group.Variants())
    {
        const auto reason = IneligibleReason(variant, requireWeight);
        if (reason)
        {
            report.Ignored.push_back({variant.Id, *reason});
        }
        else
        {
            eligible.push_back(&variant);
        }
    }
    std::sort(eligible.begin(), eligible.end(), StableLess);
    report.EligibleVariantCount = eligible.size();
    return eligible;
}

[[nodiscard]] StampVariantResolutionReport Failure(
    StampVariantResolutionReport report,
    const StampVariantResolutionError error)
{
    report.Error = error;
    report.Message = StampVariantResolutionErrorMessage(error);
    return report;
}

[[nodiscard]] StampVariantResolutionReport Success(
    StampVariantResolutionReport report,
    const StampVariant& variant)
{
    report.Resolved = ResolvedStampVariant{
        .VariantId = variant.Id,
        .Stamp = variant.Stamp,
        .DisplayOrder = variant.DisplayOrder};
    return report;
}

[[nodiscard]] std::uint64_t BoundedIndex(
    std::uint64_t random,
    const std::uint64_t bound) noexcept
{
    // Rejection sampling removes modulo bias while retaining a fully specified
    // integer-only sequence on every platform.
    const std::uint64_t threshold = (0U - bound) % bound;
    while (random < threshold) random = SplitMix64(random);
    return random % bound;
}

[[nodiscard]] double UnitInterval53(const std::uint64_t random) noexcept
{
    static_assert(std::numeric_limits<double>::is_iec559);
    constexpr double inverseTwoTo53 = 1.0 / 9007199254740992.0;
    return static_cast<double>(random >> 11U) * inverseTwoTo53;
}
}

namespace VoxelForge::Editor::Stamps
{

StampVariantResolutionReport ResolveVariant(
    const StampVariantGroup& group,
    const std::uint64_t sessionSeed,
    const std::uint64_t placementOrdinal)
{
    StampVariantResolutionReport report{
        .SelectionSeed = SelectionSeed(group, sessionSeed, placementOrdinal),
        .PlacementOrdinal = placementOrdinal};

    if (group.SelectionMode() == StampVariantSelectionMode::Fixed)
    {
        report.EligibleVariantCount = group.Variants().size();
        if (!group.FixedVariantId())
        {
            return Failure(
                std::move(report),
                StampVariantResolutionError::MissingFixedSelection);
        }
        const auto found = std::find_if(
            group.Variants().begin(), group.Variants().end(),
            [&group](const StampVariant& variant)
            {
                return variant.Id == *group.FixedVariantId();
            });
        if (found == group.Variants().end())
        {
            return Failure(
                std::move(report),
                StampVariantResolutionError::FixedVariantNotFound);
        }
        if (!found->Enabled)
        {
            report.Ignored.push_back(
                {found->Id, StampVariantSkipReason::Disabled});
            report.EligibleVariantCount = 0U;
            return Failure(
                std::move(report),
                StampVariantResolutionError::FixedVariantDisabled);
        }
        if (found->SourceState != StampVariantSourceState::Available)
        {
            report.Ignored.push_back({
                found->Id,
                found->SourceState == StampVariantSourceState::Missing
                    ? StampVariantSkipReason::MissingSource
                    : StampVariantSkipReason::ContentHashMismatch});
            report.EligibleVariantCount = 0U;
            return Failure(
                std::move(report),
                StampVariantResolutionError::FixedVariantUnavailable);
        }
        report.EligibleVariantCount = 1U;
        return Success(std::move(report), *found);
    }

    const bool weighted =
        group.SelectionMode() == StampVariantSelectionMode::Weighted;
    auto eligible = EligibleVariants(group, weighted, report);
    if (eligible.empty())
    {
        return Failure(
            std::move(report), StampVariantResolutionError::NoValidVariants);
    }
    if (eligible.size() == 1U)
    {
        return Success(std::move(report), *eligible.front());
    }

    switch (group.SelectionMode())
    {
    case StampVariantSelectionMode::Sequential:
        return Success(
            std::move(report),
            *eligible[placementOrdinal % eligible.size()]);
    case StampVariantSelectionMode::Random:
    {
        const auto index = static_cast<std::size_t>(BoundedIndex(
            report.SelectionSeed,
            static_cast<std::uint64_t>(eligible.size())));
        return Success(std::move(report), *eligible[index]);
    }
    case StampVariantSelectionMode::Weighted:
    {
        double maximumWeight = 0.0;
        for (const StampVariant* variant : eligible)
        {
            maximumWeight = std::max(maximumWeight, variant->Weight);
        }
        double total = 0.0;
        for (const StampVariant* variant : eligible)
        {
            total += variant->Weight / maximumWeight;
        }
        if (!std::isfinite(total) || total <= 0.0)
        {
            return Failure(
                std::move(report),
                StampVariantResolutionError::NoValidVariants);
        }

        const double target = UnitInterval53(report.SelectionSeed) * total;
        double cumulative = 0.0;
        for (std::size_t index = 0U; index < eligible.size(); ++index)
        {
            cumulative += eligible[index]->Weight / maximumWeight;
            // The final candidate owns the remaining half-open interval. This
            // is an exact boundary rule, not a fallback to another variant.
            if (target < cumulative || index + 1U == eligible.size())
            {
                return Success(std::move(report), *eligible[index]);
            }
        }
        break;
    }
    case StampVariantSelectionMode::Fixed:
        break;
    }

    return Failure(
        std::move(report),
        StampVariantResolutionError::UnsupportedSelectionMode);
}

std::uint64_t RenewSessionSeed(const std::uint64_t currentSeed) noexcept
{
    std::uint64_t renewed = SplitMix64(currentSeed ^ 0xd1b54a32d192ed03ULL);
    if (renewed == currentSeed)
    {
        renewed ^= 0x9e3779b97f4a7c15ULL;
    }
    return renewed;
}

} // namespace VoxelForge::Editor::Stamps
