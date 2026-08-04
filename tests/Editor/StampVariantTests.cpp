#include "VoxelStamps/Variants/StampVariantGroup.h"
#include "VoxelStamps/Variants/StampVariantResolver.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

StampVariant MakeVariant(
    const std::uint64_t variantId,
    const std::uint64_t stampId,
    const std::uint32_t displayOrder,
    const double weight = 1.0,
    const bool enabled = true,
    const StampVariantSourceState sourceState =
        StampVariantSourceState::Available)
{
    return {
        .Id = Core::UUID{variantId},
        .Stamp = {
            .Id = Core::UUID{stampId},
            .ContentHash = "variant-hash-" + std::to_string(stampId),
            .RelativePath =
                "Trees/tree-" + std::to_string(stampId) + ".vfstamp",
            .Scope = StampLibraryScope::Project},
        .Weight = weight,
        .Enabled = enabled,
        .DisplayOrder = displayOrder,
        .SourceState = sourceState};
}

StampVariantGroup MakeGroup(
    const StampVariantSelectionMode mode,
    std::vector<StampVariant> variants,
    const std::optional<Core::UUID> fixedVariantId = std::nullopt,
    const std::uint64_t groupId = 0x5354414d503139ULL)
{
    StampVariantGroupValidation validation{};
    auto group = StampVariantGroup::TryCreate(
        {
            .Id = Core::UUID{groupId},
            .Name = "Trees",
            .Category = "Nature/Trees",
            .Tags = {"nature", "tree"},
            .PrimaryThumbnailStampId = Core::UUID{101U},
            .SelectionMode = mode,
            .SeedPolicy = StampVariantSeedPolicy::PlacementSession,
            .Revision = 7U,
            .FixedVariantId = fixedVariantId,
            .Variants = std::move(variants)},
        &validation);
    Require(group && validation.IsValid(),
        "Smart Variant group fixture must be valid.");
    return std::move(*group);
}

std::size_t CountIgnored(
    const StampVariantResolutionReport& report,
    const StampVariantSkipReason reason)
{
    return static_cast<std::size_t>(std::count_if(
        report.Ignored.begin(), report.Ignored.end(),
        [reason](const StampVariantResolutionDiagnostic& diagnostic)
        {
            return diagnostic.Reason == reason;
        }));
}

void TestGroupDomainAndPortableReferences()
{
    const StampVariant repeatedStamp = MakeVariant(2U, 101U, 1U);
    const auto group = MakeGroup(
        StampVariantSelectionMode::Sequential,
        {MakeVariant(1U, 101U, 0U), repeatedStamp});
    Require(group.Variants().size() == 2U &&
                group.Variants()[0].Stamp.Id ==
                    group.Variants()[1].Stamp.Id &&
                group.Variants()[0].Id != group.Variants()[1].Id,
        "One Stamp must be allowed in multiple distinct variants.");
    Require(group.Name() == "Trees" && group.Category() == "Nature/Trees" &&
                group.Tags().size() == 2U && group.Revision() == 7U &&
                group.PrimaryThumbnailStampId() == Core::UUID{101U},
        "Smart Variant group metadata must remain exact.");

    StampVariantGroupDefinition invalid{
        .Id = Core::UUID{0U},
        .SelectionMode = StampVariantSelectionMode::Sequential,
        .Variants = {MakeVariant(1U, 101U, 0U)}};
    StampVariantGroupValidation validation{};
    Require(!StampVariantGroup::TryCreate(invalid, &validation) &&
                validation.Error ==
                    StampVariantGroupError::InvalidGroupIdentity,
        "A zero group UUID must be rejected explicitly.");

    invalid.Id = Core::UUID{10U};
    invalid.Variants.push_back(invalid.Variants.front());
    Require(!StampVariantGroup::TryCreate(invalid, &validation) &&
                validation.Error ==
                    StampVariantGroupError::DuplicateVariantIdentity &&
                validation.VariantId == Core::UUID{1U},
        "Duplicate variant UUIDs must be rejected explicitly.");

    invalid.Variants = {MakeVariant(3U, 103U, 0U)};
    invalid.Variants.front().Stamp.RelativePath = "../outside.vfstamp";
    Require(!StampVariantGroup::TryCreate(invalid, &validation) &&
                validation.Error ==
                    StampVariantGroupError::InvalidStampReference,
        "A variant reference must remain portable and confined.");

    invalid.Variants = {MakeVariant(4U, 104U, 0U)};
    invalid.SelectionMode =
        static_cast<StampVariantSelectionMode>(255U);
    Require(!StampVariantGroup::TryCreate(invalid, &validation) &&
                validation.Error ==
                    StampVariantGroupError::InvalidSelectionMode,
        "Unknown selection modes must not enter the immutable domain.");
}

void TestFixedModeNeverFallsBack()
{
    auto selected = MakeVariant(12U, 112U, 1U, 0.0);
    const auto fixed = MakeGroup(
        StampVariantSelectionMode::Fixed,
        {MakeVariant(11U, 111U, 0U), selected}, Core::UUID{12U});
    const auto resolved = ResolveVariant(fixed, 99U, 42U);
    Require(resolved.Succeeded() &&
                resolved.Resolved->VariantId == Core::UUID{12U} &&
                resolved.Resolved->Stamp.Id == Core::UUID{112U},
        "Fixed mode must return the exact manually selected UUID even when weight is unused.");

    auto missing = selected;
    missing.SourceState = StampVariantSourceState::Missing;
    const auto missingGroup = MakeGroup(
        StampVariantSelectionMode::Fixed,
        {MakeVariant(11U, 111U, 0U), missing}, Core::UUID{12U});
    const auto unavailable = ResolveVariant(missingGroup, 99U, 42U);
    Require(!unavailable.Succeeded() && !unavailable.Resolved &&
                unavailable.Error ==
                    StampVariantResolutionError::FixedVariantUnavailable &&
                CountIgnored(unavailable,
                    StampVariantSkipReason::MissingSource) == 1U,
        "A missing fixed variant must fail instead of selecting another entry.");

    selected.Enabled = false;
    const auto disabledGroup = MakeGroup(
        StampVariantSelectionMode::Fixed,
        {MakeVariant(11U, 111U, 0U), selected}, Core::UUID{12U});
    Require(ResolveVariant(disabledGroup, 99U, 42U).Error ==
                StampVariantResolutionError::FixedVariantDisabled,
        "A disabled fixed variant must fail explicitly.");

    const auto staleSelection = MakeGroup(
        StampVariantSelectionMode::Fixed,
        {MakeVariant(11U, 111U, 0U)}, Core::UUID{99U});
    Require(ResolveVariant(staleSelection, 99U, 42U).Error ==
                StampVariantResolutionError::FixedVariantNotFound,
        "A stale fixed UUID must never fall back arbitrarily.");

    const auto noSelection = MakeGroup(
        StampVariantSelectionMode::Fixed,
        {MakeVariant(11U, 111U, 0U)});
    Require(ResolveVariant(noSelection, 99U, 42U).Error ==
                StampVariantResolutionError::MissingFixedSelection,
        "Fixed mode without a selected UUID must fail clearly.");
}

void TestSequentialUsesStableDisplayOrder()
{
    const auto group = MakeGroup(
        StampVariantSelectionMode::Sequential,
        {MakeVariant(30U, 130U, 20U),
         MakeVariant(20U, 120U, 10U),
         MakeVariant(10U, 110U, 10U),
         MakeVariant(40U, 140U, 0U, 1.0, false),
         MakeVariant(50U, 150U, 0U, 1.0, true,
             StampVariantSourceState::Missing)});
    const std::vector<std::uint64_t> expected{10U, 20U, 30U, 10U, 20U};
    for (std::uint64_t ordinal = 0U; ordinal < expected.size(); ++ordinal)
    {
        const auto report = ResolveVariant(group, 555U, ordinal);
        Require(report.Succeeded() &&
                    report.Resolved->VariantId.Value() == expected[ordinal] &&
                    report.EligibleVariantCount == 3U,
            "Sequential mode must use DisplayOrder, UUID tie-break, and stable wraparound.");
    }
    Require(ResolveVariant(group, 999U, 0U).Resolved->VariantId ==
                ResolveVariant(group, 1U, 0U).Resolved->VariantId,
        "Sequential mode must not be changed by the random session seed.");
}

void TestRandomIsTupleDeterministicAndOrderIndependent()
{
    std::vector<StampVariant> variants{
        MakeVariant(3U, 103U, 2U),
        MakeVariant(1U, 101U, 0U),
        MakeVariant(2U, 102U, 1U),
        MakeVariant(4U, 104U, 3U, 1.0, false)};
    const auto first = MakeGroup(
        StampVariantSelectionMode::Random, variants);
    std::reverse(variants.begin(), variants.end());
    const auto reordered = MakeGroup(
        StampVariantSelectionMode::Random, variants);

    const auto baseline = ResolveVariant(first, 0x12345678U, 17U);
    const auto repeated = ResolveVariant(first, 0x12345678U, 17U);
    const auto afterReorder = ResolveVariant(reordered, 0x12345678U, 17U);
    Require(baseline.Succeeded() && baseline.Resolved == repeated.Resolved &&
                baseline.Resolved == afterReorder.Resolved &&
                baseline.SelectionSeed == 0xe25d0aba72e7971dULL &&
                baseline.Resolved->VariantId == Core::UUID{3U},
        "The same group/session/ordinal tuple must return the same exact UUID independent of input order.");

    bool observedAnotherVariant = false;
    for (std::uint64_t ordinal = 0U; ordinal < 64U; ++ordinal)
    {
        const auto report = ResolveVariant(first, 0x12345678U, ordinal);
        Require(report.Succeeded() &&
                    report.Resolved->VariantId != Core::UUID{4U},
            "Random mode must ignore disabled variants.");
        observedAnotherVariant = observedAnotherVariant ||
            report.Resolved->VariantId != baseline.Resolved->VariantId;
    }
    Require(observedAnotherVariant,
        "Random mode must produce a deterministic sequence, not one fixed entry.");

    const auto otherGroup = MakeGroup(
        StampVariantSelectionMode::Random,
        {MakeVariant(1U, 101U, 0U), MakeVariant(2U, 102U, 1U)},
        std::nullopt, 0x5354414d503140ULL);
    Require(ResolveVariant(otherGroup, 0x12345678U, 17U).SelectionSeed !=
                baseline.SelectionSeed,
        "GroupUUID must participate in the official selection seed.");
}

void TestWeightedRulesAndExactBoundaries()
{
    const auto group = MakeGroup(
        StampVariantSelectionMode::Weighted,
        {MakeVariant(1U, 101U, 0U, 1.0),
         MakeVariant(2U, 102U, 1U, 3.0),
         MakeVariant(3U, 103U, 2U, 100.0, true,
             StampVariantSourceState::Missing),
         MakeVariant(4U, 104U, 3U, 100.0, false),
         MakeVariant(5U, 105U, 4U, 0.0),
         MakeVariant(6U, 106U, 5U, -1.0),
         MakeVariant(7U, 107U, 6U,
             std::numeric_limits<double>::infinity()),
         MakeVariant(8U, 108U, 7U,
             std::numeric_limits<double>::quiet_NaN()),
         MakeVariant(9U, 109U, 8U, 100.0, true,
             StampVariantSourceState::ContentHashMismatch)});

    std::size_t firstCount = 0U;
    constexpr std::size_t sampleCount = 10000U;
    for (std::uint64_t ordinal = 0U; ordinal < sampleCount; ++ordinal)
    {
        const auto report = ResolveVariant(group, 777U, ordinal);
        Require(report.Succeeded() && report.EligibleVariantCount == 2U &&
                    (report.Resolved->VariantId == Core::UUID{1U} ||
                     report.Resolved->VariantId == Core::UUID{2U}),
            "Weighted mode must select only enabled, available, positive finite entries.");
        const double unit = static_cast<double>(report.SelectionSeed >> 11U) /
            9007199254740992.0;
        const Core::UUID expected =
            unit < 0.25 ? Core::UUID{1U} : Core::UUID{2U};
        Require(report.Resolved->VariantId == expected,
            "Weighted half-open boundaries must match normalized 1:3 weights exactly.");
        firstCount += report.Resolved->VariantId == Core::UUID{1U} ? 1U : 0U;

        if (ordinal == 0U)
        {
            Require(report.Ignored.size() == 7U &&
                        CountIgnored(report,
                            StampVariantSkipReason::MissingSource) == 1U &&
                        CountIgnored(report,
                            StampVariantSkipReason::Disabled) == 1U &&
                        CountIgnored(report,
                            StampVariantSkipReason::InvalidWeight) == 4U &&
                        CountIgnored(report,
                            StampVariantSkipReason::ContentHashMismatch) == 1U,
                "Weighted diagnostics must explain every ignored entry.");
        }
    }
    Require(firstCount > 2000U && firstCount < 3000U,
        "Deterministic weighted samples must retain the normalized 1:3 distribution.");

    const auto oneRemaining = MakeGroup(
        StampVariantSelectionMode::Weighted,
        {MakeVariant(10U, 110U, 0U, 0.0),
         MakeVariant(11U, 111U, 1U, 2.0)});
    Require(ResolveVariant(oneRemaining, 1U, 99U).Resolved->VariantId ==
                Core::UUID{11U},
        "One remaining weighted variant must be selected exactly.");

    const auto zeroValid = MakeGroup(
        StampVariantSelectionMode::Weighted,
        {MakeVariant(10U, 110U, 0U, 0.0),
         MakeVariant(11U, 111U, 1U, -2.0)});
    const auto blocked = ResolveVariant(zeroValid, 1U, 99U);
    Require(!blocked.Succeeded() && !blocked.Resolved &&
                blocked.Error ==
                    StampVariantResolutionError::NoValidVariants &&
                blocked.EligibleVariantCount == 0U,
        "Zero valid weighted variants must block without an arbitrary fallback.");
}

void TestExplicitSeedRenewal()
{
    constexpr std::uint64_t current = 0x123456789abcdef0ULL;
    const std::uint64_t renewed = RenewSessionSeed(current);
    Require(renewed == 0x0ae21eec5780a98fULL &&
                RenewSessionSeed(current) == renewed,
        "Explicit seed renewal must be stable and must change the session seed.");

    const auto group = MakeGroup(
        StampVariantSelectionMode::Random,
        {MakeVariant(1U, 101U, 0U), MakeVariant(2U, 102U, 1U)});
    Require(ResolveVariant(group, current, 0U).SelectionSeed !=
                ResolveVariant(group, renewed, 0U).SelectionSeed,
        "Renew Seed must create a new stable selection sequence.");
}

} // namespace

int main()
{
    try
    {
        TestGroupDomainAndPortableReferences();
        TestFixedModeNeverFallsBack();
        TestSequentialUsesStableDisplayOrder();
        TestRandomIsTupleDeterministicAndOrderIndependent();
        TestWeightedRulesAndExactBoundaries();
        TestExplicitSeedRenewal();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp Variant tests passed.\n";
    return EXIT_SUCCESS;
}
