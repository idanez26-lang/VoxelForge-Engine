#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Library/StampAssetCache.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelStamps/Variants/StampVariantGroup.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {32U, 4U, 4U}});
    const auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "stamp-variant-placement-test.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create Smart Variant placement document.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeStamp(
    const std::uint64_t id,
    std::string hash,
    const StampColor color)
{
    StampValidationResult validation{};
    auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{id}, std::move(hash)},
        {{0, 0, 0}, {0, 0, 0}, {1U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {}},
        {}, {{0U, color}}, {{{0, 0, 0}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create Smart Variant Stamp fixture.");
    return std::move(*stamp);
}

class MemoryStampRepository final : public IStampLibraryRepository
{
public:
    StampAssetReference Add(
        VoxelStamp stamp,
        std::filesystem::path relativePath)
    {
        StampAssetReference reference{
            .Id = stamp.Identity().Id,
            .ContentHash = stamp.Identity().ContentHash,
            .RelativePath = std::move(relativePath),
            .Scope = StampLibraryScope::Project};
        entries_.push_back(
            {.Reference = reference, .Stamp = std::move(stamp)});
        return reference;
    }

    bool SetAvailable(
        const StampAssetReference& reference,
        const bool available) noexcept
    {
        Entry* entry = Find(reference);
        if (entry == nullptr) return false;
        entry->Available = available;
        return true;
    }

    StampLibraryResult Install(
        const VoxelStamp&,
        const StampInstallOptions&) override
    {
        return Failure(StampLibraryError::TransactionFailed);
    }

    StampLibraryResult Read(
        const StampAssetReference& reference) const override
    {
        const Entry* entry = Find(reference);
        if (entry == nullptr || !entry->Available)
            return Failure(StampLibraryError::AssetNotFound, reference);
        return {.Reference = reference, .Stamp = entry->Stamp};
    }

    StampLibrarySourceFactsResult InspectSource(
        const StampAssetReference& reference) const override
    {
        const Entry* entry = Find(reference);
        if (entry == nullptr || !entry->Available)
        {
            return {
                .Error = StampLibraryError::AssetNotFound,
                .Message = std::string(StampLibraryErrorMessage(
                    StampLibraryError::AssetNotFound))};
        }
        return {
            .Facts = StampLibrarySourceFacts{
                .FileBytes = entry->Stamp.RetainedBytes()}};
    }

    StampLibraryResult EnumerateSourceAssets() const override
    {
        StampLibraryResult result;
        for (const Entry& entry : entries_)
        {
            if (entry.Available)
            {
                result.Assets.push_back({
                    .Reference = entry.Reference,
                    .FileBytes = entry.Stamp.RetainedBytes()});
            }
        }
        return result;
    }

    StampLibraryResult Remove(
        const StampAssetReference& reference) override
    {
        Entry* entry = Find(reference);
        if (entry == nullptr || !entry->Available)
            return Failure(StampLibraryError::AssetNotFound, reference);
        entry->Available = false;
        return {.Reference = reference};
    }

    StampLibraryResult ResolvePortableReference(
        const std::filesystem::path& relativePath) const override
    {
        for (const Entry& entry : entries_)
        {
            if (entry.Available &&
                entry.Reference.RelativePath == relativePath)
            {
                return {
                    .Reference = entry.Reference,
                    .Stamp = entry.Stamp};
            }
        }
        return Failure(StampLibraryError::AssetNotFound);
    }

    StampLibraryResult RebuildSourceInventory() const override
    {
        return EnumerateSourceAssets();
    }

private:
    struct Entry final
    {
        StampAssetReference Reference;
        VoxelStamp Stamp;
        bool Available = true;
    };

    static StampLibraryResult Failure(
        const StampLibraryError error,
        StampAssetReference reference = {})
    {
        return {
            .Error = error,
            .Message = std::string(StampLibraryErrorMessage(error)),
            .Reference = std::move(reference)};
    }

    Entry* Find(const StampAssetReference& reference) noexcept
    {
        for (Entry& entry : entries_)
        {
            if (entry.Reference == reference) return &entry;
        }
        return nullptr;
    }

    const Entry* Find(const StampAssetReference& reference) const noexcept
    {
        for (const Entry& entry : entries_)
        {
            if (entry.Reference == reference) return &entry;
        }
        return nullptr;
    }

    std::vector<Entry> entries_;
};

class EditSession final : public VoxelEditSession
{
public:
    explicit EditSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document)
    {
    }

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return 20U;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override
    {
        return nullptr;
    }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++Rebuilds;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override { ++Completions; }

    std::size_t Rebuilds = 0U;
    std::size_t Completions = 0U;

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
};

StampVariant MakeVariant(
    const std::uint64_t variantId,
    StampAssetReference reference,
    const std::uint32_t order,
    const double weight = 1.0,
    const StampVariantSourceState sourceState =
        StampVariantSourceState::Available)
{
    return {
        .Id = Core::UUID{variantId},
        .Stamp = std::move(reference),
        .Weight = weight,
        .DisplayOrder = order,
        .SourceState = sourceState};
}

StampVariantGroup MakeGroup(
    const StampVariantSelectionMode mode,
    std::vector<StampVariant> variants,
    const std::optional<Core::UUID> fixed = std::nullopt)
{
    StampVariantGroupValidation validation{};
    auto group = StampVariantGroup::TryCreate(
        {
            .Id = Core::UUID{0x47524f55503230ULL},
            .Name = "Placement Variants",
            .Category = "Tests",
            .SelectionMode = mode,
            .Revision = 3U,
            .FixedVariantId = fixed,
            .Variants = std::move(variants)},
        &validation);
    Require(group && validation.IsValid(),
        "Unable to create Smart Variant group fixture.");
    return std::move(*group);
}

struct VariantFixture final
{
    MemoryStampRepository Repository;
    StampAssetReference First;
    StampAssetReference Second;
    StampAssetReference Third;

    VariantFixture()
    {
        First = Repository.Add(
            MakeStamp(201U, "variant-first",
                {220U, 20U, 30U, 255U}),
            "Variants/first.vfstamp");
        Second = Repository.Add(
            MakeStamp(202U, "variant-second",
                {20U, 220U, 30U, 255U}),
            "Variants/second.vfstamp");
        Third = Repository.Add(
            MakeStamp(203U, "variant-third",
                {20U, 30U, 220U, 255U}),
            "Variants/third.vfstamp");
    }
};

void TestSuccessiveVariantsAndSourceIndependentRedo()
{
    VariantFixture fixture;
    StampAssetCache cache(fixture.Repository);
    auto document = MakeDocument();
    EditSession editSession(document);
    VoxelEditHistory history;
    StampPlacementSession session;
    const StampVariantGroup group = MakeGroup(
        StampVariantSelectionMode::Sequential,
        {MakeVariant(1001U, fixture.First, 0U),
         MakeVariant(1002U, fixture.Second, 1U)});

    const auto begun = session.BeginVariantGroup(
        group, cache, 0x20U, document, 20U, 0U,
        {StampFixedPoint::UnitsPerVoxel, 0, 0});
    Require(begun.Succeeded && session.IsVariantPlacement() &&
                session.PlacementOrdinal() == 0U &&
                session.CurrentVariantIdentity() &&
                session.CurrentVariantIdentity()->VariantId ==
                    Core::UUID{1001U} &&
                session.CurrentPlan()->Variant ==
                    *session.CurrentVariantIdentity(),
        "Variant placement must begin with the exact ordinal-zero identity.");

    const auto first = session.PlaceOnce(
        document, 20U, editSession, history);
    Require(first && first.History.StampVariantMetadata &&
                first.History.StampVariantMetadata->VariantId ==
                    Core::UUID{1001U} &&
                first.History.StampVariantMetadata->StampId ==
                    fixture.First.Id &&
                first.History.StampVariantMetadata->PlacementOrdinal == 0U &&
                session.PlacementOrdinal() == 1U &&
                session.CurrentVariantIdentity()->VariantId ==
                    Core::UUID{1002U},
        "A successful placement must store its exact variant and advance to the next ordinal.");

    Require(session.SetTarget(
                {3 * StampFixedPoint::UnitsPerVoxel, 0, 0},
                document, 20U).Succeeded,
        "Unable to move the second variant preview.");
    const auto second = session.PlaceOnce(
        document, 20U, editSession, history);
    Require(second && second.History.StampVariantMetadata &&
                second.History.StampVariantMetadata->VariantId ==
                    Core::UUID{1002U} &&
                second.History.StampVariantMetadata->StampId ==
                    fixture.Second.Id &&
                second.History.StampVariantMetadata->PlacementOrdinal == 1U &&
                session.PlacementOrdinal() == 2U &&
                session.CurrentVariantIdentity()->VariantId ==
                    Core::UUID{1001U},
        "The second placement must store the exact next variant identity.");

    const auto placedVoxel = document.GetVoxel({3, 0, 0});
    Require(placedVoxel.has_value(),
        "The second Smart Variant was not placed.");
    const auto undone = history.Undo(editSession);
    Require(undone && undone.StampVariantMetadata &&
                undone.StampVariantMetadata->VariantId ==
                    Core::UUID{1002U} &&
                !document.GetVoxel({3, 0, 0}),
        "Undo must expose and remove the exact chosen variant operation.");

    Require(fixture.Repository.Remove(fixture.Second).Succeeded() &&
                cache.Invalidate(fixture.Second),
        "Unable to remove the chosen source before Redo.");
    const StampAssetCacheMetrics beforeRedo = cache.Metrics();
    const StampVariantResolutionReport resolutionBeforeRedo =
        *session.CurrentVariantResolution();
    const auto redone = history.Redo(editSession);
    const StampAssetCacheMetrics afterRedo = cache.Metrics();
    Require(redone && redone.StampVariantMetadata &&
                redone.StampVariantMetadata->VariantId ==
                    Core::UUID{1002U} &&
                redone.StampVariantMetadata->StampId == fixture.Second.Id &&
                redone.StampVariantMetadata->ReplayPolicy ==
                    VoxelEditStampReplayPolicy::RestoreStoredOperation &&
                document.GetVoxel({3, 0, 0}) == placedVoxel &&
                beforeRedo.Hits == afterRedo.Hits &&
                beforeRedo.Misses == afterRedo.Misses &&
                beforeRedo.LoadAttempts == afterRedo.LoadAttempts &&
                beforeRedo.LoadFailures == afterRedo.LoadFailures &&
                session.CurrentVariantResolution()->SelectionSeed ==
                    resolutionBeforeRedo.SelectionSeed &&
                session.CurrentVariantResolution()->Resolved ==
                    resolutionBeforeRedo.Resolved &&
                session.CurrentVariantResolution()->PlacementOrdinal ==
                    resolutionBeforeRedo.PlacementOrdinal,
        "Redo must restore stored voxels and identity without cache access or reroll after source deletion.");
}

void TestFailuresNeverConsumeOrdinal()
{
    VariantFixture fixture;
    StampAssetCache cache(fixture.Repository);
    auto document = MakeDocument();
    EditSession editSession(document);

    StampPlacementSession invalidGroupSession;
    const StampVariantGroup zeroValid = MakeGroup(
        StampVariantSelectionMode::Weighted,
        {MakeVariant(1001U, fixture.First, 0U, 0.0),
         MakeVariant(1002U, fixture.Second, 1U, -1.0)});
    const auto unresolved = invalidGroupSession.BeginVariantGroup(
        zeroValid, cache, 1U, document, 20U);
    VoxelEditHistory unresolvedHistory;
    const auto blocked = invalidGroupSession.PlaceOnce(
        document, 20U, editSession, unresolvedHistory);
    Require(unresolved.Code ==
                StampPlacementSessionResultCode::VariantResolutionFailed &&
                unresolved.VariantError ==
                    StampVariantResolutionError::NoValidVariants &&
                invalidGroupSession.IsVariantPlacement() &&
                !invalidGroupSession.CurrentPlan() &&
                blocked.Status == StampPlacementSessionPlaceStatus::Rejected &&
                invalidGroupSession.PlacementOrdinal() == 0U &&
                unresolvedHistory.UndoCount() == 0U,
        "Zero valid variants must block without consuming an ordinal or history.");

    StampPlacementSession outOfBoundsSession;
    const StampVariantGroup sequential = MakeGroup(
        StampVariantSelectionMode::Sequential,
        {MakeVariant(1001U, fixture.First, 0U)});
    const auto outside = outOfBoundsSession.BeginVariantGroup(
        sequential, cache, 2U, document, 20U, 0U,
        {100 * StampFixedPoint::UnitsPerVoxel, 0, 0});
    VoxelEditHistory outsideHistory;
    const auto rejected = outOfBoundsSession.PlaceOnce(
        document, 20U, editSession, outsideHistory);
    Require(outside.Succeeded && outOfBoundsSession.CurrentPlan() &&
                !outOfBoundsSession.CurrentPlan()->CanCommit &&
                outOfBoundsSession.CurrentPlan()->HasErrors() &&
                rejected.Status == StampPlacementSessionPlaceStatus::Rejected &&
                outOfBoundsSession.PlacementOrdinal() == 0U &&
                outsideHistory.UndoCount() == 0U,
        "An invalid variant plan must not consume its ordinal.");

    StampPlacementSession limitedSession;
    Require(limitedSession.BeginVariantGroup(
                sequential, cache, 3U, document, 20U, 0U,
                {5 * StampFixedPoint::UnitsPerVoxel, 0, 0}).Succeeded,
        "Unable to begin history-limit Smart Variant fixture.");
    VoxelEditHistory limitedHistory({
        .MaximumCommandCount = 100U,
        .MaximumEstimatedMemory = 1U});
    const auto limited = limitedSession.PlaceOnce(
        document, 20U, editSession, limitedHistory);
    Require(limited.Status == StampPlacementSessionPlaceStatus::Rejected &&
                limited.History.Code ==
                    VoxelEditHistoryResultCode::LimitExceeded &&
                limitedSession.PlacementOrdinal() == 0U &&
                limitedHistory.UndoCount() == 0U &&
                !document.GetVoxel({5, 0, 0}),
        "A history refusal must retain the same Smart Variant ordinal.");

    Require(fixture.Repository.SetAvailable(fixture.Second, false),
        "Unable to hide the missing variant source.");
    StampPlacementSession missingSession;
    const StampVariantGroup fixedMissing = MakeGroup(
        StampVariantSelectionMode::Fixed,
        {MakeVariant(1002U, fixture.Second, 0U)}, Core::UUID{1002U});
    const auto missing = missingSession.BeginVariantGroup(
        fixedMissing, cache, 4U, document, 20U);
    VoxelEditHistory missingHistory;
    const auto missingPlacement = missingSession.PlaceOnce(
        document, 20U, editSession, missingHistory);
    Require(missing.Code ==
                StampPlacementSessionResultCode::VariantAssetUnavailable &&
                missing.LibraryError == StampLibraryError::AssetNotFound &&
                missingSession.IsVariantPlacement() &&
                missingSession.PlacementOrdinal() == 0U &&
                !missingSession.CurrentPlan() &&
                missingPlacement.Status ==
                    StampPlacementSessionPlaceStatus::Rejected &&
                missingPlacement.History.Message.find("unavailable") !=
                    std::string::npos &&
                missingHistory.UndoCount() == 0U,
        "A source that disappears after resolution must block without fallback or ordinal consumption.");
}

void TestManualSeedRenewalRebuildsOneStablePreview()
{
    VariantFixture fixture;
    StampAssetCache cache(fixture.Repository);
    auto document = MakeDocument();
    StampPlacementSession session;
    const StampVariantGroup random = MakeGroup(
        StampVariantSelectionMode::Random,
        {MakeVariant(1001U, fixture.First, 0U),
         MakeVariant(1002U, fixture.Second, 1U),
         MakeVariant(1003U, fixture.Third, 2U)});

    constexpr std::uint64_t initialSeed = 0x12345678U;
    Require(session.BeginVariantGroup(
                random, cache, initialSeed, document, 20U, 0U,
                {8 * StampFixedPoint::UnitsPerVoxel, 0, 0}).Succeeded,
        "Unable to begin Renew Seed fixture.");
    const StampPlacementCacheKey previousKey = *session.CacheKey();
    const std::uint64_t previousSelectionSeed =
        session.CurrentVariantResolution()->SelectionSeed;
    const auto renewed = session.RenewVariantSeed(document, 20U);
    Require(renewed.Succeeded &&
                session.PlacementSessionSeed() ==
                    RenewSessionSeed(initialSeed) &&
                session.PlacementOrdinal() == 0U &&
                session.CurrentVariantResolution()->SelectionSeed !=
                    previousSelectionSeed &&
                session.CurrentPlan()->Variant->PlacementSessionSeed ==
                    session.PlacementSessionSeed() &&
                session.CurrentPlan()->Variant->PlacementOrdinal == 0U &&
                *session.CacheKey() != previousKey,
        "Renew Seed must rebuild one stable preview without consuming an ordinal.");
}

void TestMismatchedVariantMetadataIsRefusedBeforeMutation()
{
    VariantFixture fixture;
    StampAssetCache cache(fixture.Repository);
    auto document = MakeDocument();
    StampPlacementSession session;
    const StampVariantGroup group = MakeGroup(
        StampVariantSelectionMode::Sequential,
        {MakeVariant(1001U, fixture.First, 0U)});
    Require(session.BeginVariantGroup(
                group, cache, 5U, document, 20U).Succeeded,
        "Unable to begin metadata validation fixture.");

    StampPlacementPlannerRequest mismatched{
        .Stamp = session.ActiveStamp(),
        .Variant = *session.CurrentVariantIdentity(),
        .Document = &document,
        .DocumentGeneration = 20U};
    mismatched.Variant->StampId = Core::UUID{999U};
    const StampPlacementPlan invalid =
        StampPlacementPlanner::Build(mismatched);
    Require(!invalid.CanCommit && invalid.Voxels.empty() &&
                invalid.HasErrors() && !invalid.Diagnostics.empty() &&
                invalid.Diagnostics.front().Code ==
                    StampPlacementDiagnosticCode::InvalidVariantIdentity,
        "Planner must reject mismatched chosen Stamp UUID before voxel planning.");

    EditSession editSession(document);
    VoxelEditHistory history;
    VoxelEditOperation operation;
    operation.Label = "Invalid Stamp Variant metadata";
    operation.Changes.push_back({
        .Position = {1, 0, 0},
        .ExistsAfter = true,
        .PaletteIndexAfter = 1U});
    operation.StampVariantMetadata =
        std::make_shared<VoxelEditStampVariantMetadata>(
            VoxelEditStampVariantMetadata{
                .GroupId = group.Id(),
                .VariantId = Core::UUID{1001U},
                .StampId = Core::UUID{0U},
                .ExpectedContentHash = fixture.First.ContentHash});
    const auto refused = history.Execute(editSession, std::move(operation));
    Require(refused.Code == VoxelEditHistoryResultCode::Failed &&
                !document.GetVoxel({1, 0, 0}) &&
                history.UndoCount() == 0U,
        "Invalid stored chosen UUID must fail safely before document mutation.");
}

} // namespace

int main()
{
    try
    {
        TestSuccessiveVariantsAndSourceIndependentRedo();
        TestFailuresNeverConsumeOrdinal();
        TestManualSeedRenewalRebuildsOneStablePreview();
        TestMismatchedVariantMetadataIsRefusedBeforeMutation();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp Variant placement tests passed.\n";
    return EXIT_SUCCESS;
}
