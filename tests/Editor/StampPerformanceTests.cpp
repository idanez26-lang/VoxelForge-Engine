#include "VoxelStamps/Format/VfstampReader.h"
#include "VoxelStamps/Format/VfstampWriter.h"
#include "VoxelStamps/Library/StampAssetCache.h"
#include "VoxelStamps/Library/StampCatalogService.h"
#include "VoxelStamps/Library/StampMemoryCatalogStore.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;
using VoxelForge::Editor::VoxelPreviewData;

using Clock = std::chrono::steady_clock;
constexpr std::int32_t Fixed = StampFixedPoint::UnitsPerVoxel;

void Require(const bool condition, const char* const message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Operation>
double MeasureMilliseconds(Operation&& operation)
{
    const auto started = Clock::now();
    operation();
    return std::chrono::duration<double, std::milli>(
        Clock::now() - started).count();
}

double Median(std::vector<double> samples)
{
    Require(!samples.empty(), "A benchmark requires at least one sample.");
    const auto middle = samples.begin() +
        static_cast<std::ptrdiff_t>(samples.size() / 2U);
    std::nth_element(samples.begin(), middle, samples.end());
    return *middle;
}

void Report(const char* const name, const double milliseconds)
{
    std::cout << "STAMP22_METRIC " << name << "=" << std::fixed
              << std::setprecision(3) << milliseconds << " ms\n";
}

Asset::Voxel::VoxelDocument MakeDocument(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z)
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {x, y, z}});
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "stamp-performance-test.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Unable to create Stamp performance document fixture.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeGridStamp(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const std::uint64_t id,
    std::string hash = "stamp-performance")
{
    std::vector<StampVoxel> voxels;
    const std::size_t count = static_cast<std::size_t>(x) * y * z;
    voxels.reserve(count);
    for (std::uint32_t currentY = 0U; currentY < y; ++currentY)
    {
        for (std::uint32_t currentZ = 0U; currentZ < z; ++currentZ)
        {
            for (std::uint32_t currentX = 0U; currentX < x; ++currentX)
            {
                voxels.push_back({
                    .Position = {
                        static_cast<std::int32_t>(currentX),
                        static_cast<std::int32_t>(currentY),
                        static_cast<std::int32_t>(currentZ)},
                    .LocalColorId = 0U});
            }
        }
    }

    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{id}, std::move(hash)},
        {{},
         {static_cast<std::int32_t>(x - 1U),
          static_cast<std::int32_t>(y - 1U),
          static_cast<std::int32_t>(z - 1U)},
         {x, y, z}},
        {.RequestedMode = StampPivotMode::Corner,
         .ResolvedMode = StampPivotMode::Corner,
         .LocalPosition = {}},
        {},
        {{0U, Asset::Vox::DefaultVoxPalette()[1U]}},
        std::move(voxels), DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Unable to create Stamp performance fixture.");
    return *stamp;
}

class CountingRepository final : public IStampLibraryRepository
{
public:
    CountingRepository(VoxelStamp stamp, StampAssetReference reference)
        : Stamp(std::move(stamp)), Reference(std::move(reference))
    {
    }

    StampLibraryResult Install(
        const VoxelStamp&,
        const StampInstallOptions&) override
    {
        return {.Error = StampLibraryError::IoFailure};
    }

    StampLibraryResult Read(
        const StampAssetReference& reference) const override
    {
        ++ReadCalls;
        if (reference != Reference)
        {
            return {.Error = StampLibraryError::AssetNotFound};
        }
        return {.Reference = Reference, .Stamp = Stamp};
    }

    StampLibrarySourceFactsResult InspectSource(
        const StampAssetReference& reference) const override
    {
        ++InspectCalls;
        if (reference != Reference)
        {
            return {.Error = StampLibraryError::AssetNotFound};
        }
        return {.Facts = StampLibrarySourceFacts{
                    .FileBytes = Stamp.RetainedBytes()}};
    }

    StampLibraryResult EnumerateSourceAssets() const override
    {
        ++EnumerateCalls;
        return {};
    }

    StampLibraryResult Remove(const StampAssetReference&) override
    {
        return {.Error = StampLibraryError::IoFailure};
    }

    StampLibraryResult ResolvePortableReference(
        const std::filesystem::path&) const override
    {
        return {.Error = StampLibraryError::InvalidReference};
    }

    StampLibraryResult RebuildSourceInventory() const override
    {
        ++RebuildCalls;
        return {};
    }

    VoxelStamp Stamp;
    StampAssetReference Reference;
    mutable std::uint64_t ReadCalls = 0U;
    mutable std::uint64_t InspectCalls = 0U;
    mutable std::uint64_t EnumerateCalls = 0U;
    mutable std::uint64_t RebuildCalls = 0U;
};

void TestHardLimitsRejectBeforeProportionalWork()
{
    VoxelStamp stamp = MakeGridStamp(2U, 2U, 2U, 2201U, {});
    const VfstampWriteResult written = WriteVfstampBytes(stamp);
    Require(written.IsSuccess(),
        "Unable to create hard-limit Vfstamp fixture.");
    StampResourceLimits readerLimits = DefaultStampResourceLimits();
    readerLimits.SoftVoxelCount = 7U;
    readerLimits.HardVoxelCount = 7U;
    const VfstampReadResult rejected =
        ReadVfstampBytes(written.Bytes, readerLimits);
    Require(rejected.Error == VfstampReadError::DecodedLimitExceeded &&
                !rejected.Container,
        "Reader must reject a hard VOX0 count before copying payload chunks.");

    const VoxelStamp planningStamp =
        MakeGridStamp(16U, 16U, 16U, 2202U);
    auto document = MakeDocument(32U, 32U, 32U);
    StampResourceLimits plannerLimits = DefaultStampResourceLimits();
    plannerLimits.SoftVoxelCount = 4095U;
    plannerLimits.HardVoxelCount = 4095U;
    const StampPlacementPlan plan = StampPlacementPlanner::Build({
        .Stamp = &planningStamp,
        .Document = &document,
        .DocumentGeneration = 22U,
        .Transform = {.TargetPivot = {8 * Fixed, 8 * Fixed, 8 * Fixed}},
        .ResourceLimits = plannerLimits});
    Require(!plan.CanCommit && plan.Voxels.empty() &&
                plan.ResourceLimitEvaluation.Status ==
                    StampLimitStatus::HardLimitExceeded,
        "Planner must reject its hard limit before allocating planned voxels.");
}

void TestColdFormatDecoding()
{
    const VoxelStamp small = MakeGridStamp(16U, 16U, 16U, 2208U, {});
    const VfstampWriteResult smallBytes = WriteVfstampBytes(small);
    Require(smallBytes.IsSuccess(),
        "Unable to serialize the small cold-decode benchmark.");
    std::vector<double> samples;
    samples.reserve(9U);
    for (std::size_t index = 0U; index < 9U; ++index)
    {
        VfstampDecodeResult decoded;
        samples.push_back(MeasureMilliseconds([&]
        {
            decoded = DecodeVfstampBytes(smallBytes.Bytes);
        }));
        Require(decoded.IsSuccess() && decoded.Stamp->Voxels().size() == 4096U,
            "Small cold-decode benchmark failed.");
    }
    Report("cold_decode_4096_median", Median(std::move(samples)));

    const VoxelStamp softLimit = MakeGridStamp(64U, 64U, 64U, 2209U, {});
    const VfstampWriteResult largeBytes = WriteVfstampBytes(softLimit);
    Require(largeBytes.IsSuccess(),
        "Unable to serialize the soft-limit cold-decode benchmark.");
    VfstampDecodeResult decoded;
    const double milliseconds = MeasureMilliseconds([&]
    {
        decoded = DecodeVfstampBytes(largeBytes.Bytes);
    });
    Require(decoded.IsSuccess() &&
                decoded.Stamp->Voxels().size() == 262144U,
        "Soft-limit cold-decode benchmark failed.");
    Report("cold_decode_262144", milliseconds);
}

void TestBoundedWarmAssetCache()
{
    VoxelStamp stamp = MakeGridStamp(16U, 4U, 4U, 2203U, "cache-2203");
    StampAssetReference reference{
        .Id = stamp.Identity().Id,
        .ContentHash = stamp.Identity().ContentHash,
        .RelativePath = "Assets/ForgeLibrary/Creations/cache-2203.vfstamp"};
    CountingRepository repository(stamp, reference);
    const std::size_t budget = repository.Stamp.RetainedBytes();
    StampAssetCache cache(repository, {.MemoryBudgetBytes = budget});
    Require(cache.GetOrLoad(reference).Succeeded(),
        "Unable to populate the warm-cache benchmark.");

    std::vector<double> samples;
    samples.reserve(101U);
    for (std::size_t index = 0U; index < 101U; ++index)
    {
        StampAssetCacheResult result;
        samples.push_back(MeasureMilliseconds(
            [&] { result = cache.GetOrLoad(reference); }));
        Require(result.Succeeded() && result.CacheHit,
            "Warm cache lookup unexpectedly reparsed the Stamp.");
    }
    const StampAssetCacheMetrics metrics = cache.Metrics();
    Require(repository.ReadCalls == 1U && metrics.Loads == 1U &&
                metrics.Hits == 101U && metrics.Entries == 1U &&
                metrics.RetainedBytes <= metrics.MemoryBudgetBytes,
        "Warm cache must avoid reparsing and remain within its byte budget.");
    Report("warm_cache_lookup_median", Median(std::move(samples)));
}

StampCatalog MakeCatalogue(const std::size_t count)
{
    StampCatalog catalogue;
    catalogue.Entries.reserve(count);
    for (std::size_t index = 0U; index < count; ++index)
    {
        const std::string number = std::to_string(index);
        catalogue.Entries.push_back({
            .Reference = {
                .Id = Core::UUID{static_cast<std::uint64_t>(index + 1U)},
                .ContentHash = "hash-" + number,
                .RelativePath = "Assets/ForgeLibrary/Creations/item-" +
                    number + ".vfstamp"},
            .FileName = "item-" + number + ".vfstamp",
            .FileBytes = 128U,
            .Dimensions = {1U, 1U, 1U},
            .VoxelCount = 1U,
            .PaletteCount = 1U});
    }
    return catalogue;
}

void TestCatalogueQueriesNeverScanSources()
{
    VoxelStamp stamp = MakeGridStamp(1U, 1U, 1U, 2204U, "catalog-2204");
    StampAssetReference unusedReference{
        .Id = stamp.Identity().Id,
        .ContentHash = stamp.Identity().ContentHash,
        .RelativePath = "Assets/ForgeLibrary/Creations/unused.vfstamp"};
    CountingRepository repository(stamp, unusedReference);
    StampMemoryCatalogStore store;
    Require(store.WriteCatalogueAtomically(MakeCatalogue(10000U)).Succeeded(),
        "Unable to populate the catalogue benchmark.");
    StampCatalogService service(repository, store);

    StampCatalogResult first;
    const double loadMilliseconds = MeasureMilliseconds([&]
    {
        first = service.Query({.Text = "item-9999"});
    });
    Require(first.Succeeded() && first.Catalog.Entries.size() == 1U,
        "Initial catalogue load benchmark returned an unexpected result.");
    Report("catalogue_load_and_index_10000", loadMilliseconds);

    std::vector<double> samples;
    samples.reserve(51U);
    for (std::size_t index = 0U; index < 51U; ++index)
    {
        StampCatalogResult result;
        samples.push_back(MeasureMilliseconds([&]
        {
            result = service.Query({.Text = "item-9999"});
        }));
        Require(result.Succeeded() && result.Catalog.Entries.size() == 1U,
            "Catalogue query benchmark returned an unexpected result.");
    }
    const StampCatalogServiceMetrics metrics = service.Metrics();
    Require(metrics.Queries == 52U && metrics.StoreLoads == 1U &&
                metrics.CacheHits == 51U &&
                metrics.SourceInventoryScans == 0U &&
                metrics.SourceReads == 0U && repository.ReadCalls == 0U &&
                repository.RebuildCalls == 0U &&
                repository.EnumerateCalls == 0U &&
                metrics.SearchIndexEntries == 10000U &&
                metrics.SearchIndexBytes != 0U,
        "Repeated catalogue queries must remain memory-only.");
    Report("catalogue_search_10000_median", Median(std::move(samples)));
}

StampPlacementPlan MeasurePlan(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t samples,
    const char* const metric)
{
    std::vector<double> timings;
    timings.reserve(samples);
    StampPlacementPlan latest;
    for (std::size_t index = 0U; index < samples; ++index)
    {
        timings.push_back(MeasureMilliseconds([&]
        {
            latest = StampPlacementPlanner::Build({
                .Stamp = &stamp,
                .Document = &document,
                .DocumentGeneration = 22U,
                .Transform = {.TargetPivot = {}}});
        }));
        Require(latest.WorldBounds.Valid &&
                    latest.Voxels.size() == stamp.Voxels().size(),
            "Placement benchmark failed to produce exact planned voxels.");
    }
    Report(metric, Median(std::move(timings)));
    return latest;
}

void FillOverlap(
    Asset::Voxel::VoxelDocument& document,
    const VoxelStamp& stamp,
    const std::size_t count)
{
    for (std::size_t index = 0U; index < count; ++index)
    {
        const StampLocalPosition local = stamp.Voxels()[index].Position;
        Require(document.SetVoxel(
                    {local.X, local.Y, local.Z}, 1U).Succeeded,
            "Unable to populate overlap benchmark document.");
    }
}

void TestSparseDensePlanningAndLargePreview()
{
    const VoxelStamp small = MakeGridStamp(16U, 16U, 16U, 2205U);
    const auto sparse = MakeDocument(16U, 16U, 16U);
    StampPlacementPlan sparsePlan =
        MeasurePlan(small, sparse, 9U, "placement_4096_sparse_median");
    Require(sparsePlan.Statistics.OverlapCount == 0U,
        "Sparse placement benchmark must have zero overlap.");

    auto quarter = MakeDocument(16U, 16U, 16U);
    FillOverlap(quarter, small, small.Voxels().size() / 4U);
    StampPlacementPlan quarterPlan =
        MeasurePlan(small, quarter, 9U, "placement_4096_overlap25_median");
    Require(quarterPlan.Statistics.OverlapCount == 1024U,
        "Quarter-overlap benchmark density is incorrect.");

    auto dense = MakeDocument(16U, 16U, 16U);
    FillOverlap(dense, small, small.Voxels().size());
    StampPlacementPlan densePlan =
        MeasurePlan(small, dense, 9U, "placement_4096_overlap100_median");
    Require(densePlan.Statistics.OverlapCount == small.Voxels().size(),
        "Dense placement benchmark must have complete overlap.");

    const VoxelStamp softLimit = MakeGridStamp(64U, 64U, 64U, 2206U);
    const auto largeDocument = MakeDocument(64U, 64U, 64U);
    StampPlacementPlan largePlan;
    const double planMilliseconds = MeasureMilliseconds([&]
    {
        largePlan = StampPlacementPlanner::Build({
            .Stamp = &softLimit,
            .Document = &largeDocument,
            .DocumentGeneration = 22U});
    });
    Require(largePlan.WorldBounds.Valid &&
                largePlan.Voxels.size() == 262144U,
        "Soft-limit placement benchmark must retain exact geometry.");
    Report("placement_262144_sparse", planMilliseconds);

    VoxelPreviewData preview;
    const double previewMilliseconds = MeasureMilliseconds([&]
    {
        preview = BuildStampPreview(largePlan);
    });
    Require(preview.IsActive() && preview.Voxels.size() == 262144U,
        "Large preview benchmark must retain every planned voxel.");
    Report("preview_262144", previewMilliseconds);
}

void TestUnchangedPreviewPerformsNoProportionalWork()
{
    const VoxelStamp stamp = MakeGridStamp(16U, 16U, 16U, 2207U);
    auto document = MakeDocument(32U, 32U, 32U);
    StampPlacementSession session;
    Require(session.Begin(stamp, document, 22U, 0U,
                {8 * Fixed, 8 * Fixed, 8 * Fixed}).Succeeded,
        "Unable to start idle-preview benchmark.");
    const std::uint64_t previewRevision = session.CurrentPreview()->Revision;
    session.ResetMetrics();

    const double idleMilliseconds = MeasureMilliseconds([&]
    {
        for (std::size_t index = 0U; index < 1000U; ++index)
        {
            const StampPlacementSessionResult rebuilt =
                session.Rebuild(document, 22U);
            Require(rebuilt.Succeeded && !rebuilt.PlanChanged &&
                        !rebuilt.PreviewChanged,
                "An unchanged preview rebuild request must be a cache hit.");
        }
    });
    const StampPlacementSessionMetrics idle = session.Metrics();
    Require(idle.BuildRequests == 1000U && idle.PlanCacheHits == 1000U &&
                idle.PlannerBuilds == 0U && idle.PreviewBuilds == 0U &&
                idle.PreviewChanges == 0U &&
                session.CurrentPreview()->Revision == previewRevision,
        "Idle preview must not replan, rebuild, allocate proportional data or change revision.");
    Report("idle_preview_1000_requests", idleMilliseconds);

    Require(document.SetVoxel({31, 31, 31}, 1U).Succeeded,
        "Unable to change the benchmark document revision.");
    const StampPlacementSessionResult changed = session.Rebuild(document, 22U);
    const StampPlacementSessionMetrics refreshed = session.Metrics();
    Require(changed.Succeeded && changed.PlanChanged &&
                refreshed.PlannerBuilds == 1U &&
                refreshed.PreviewBuilds == 1U,
        "A document revision change must invalidate exactly one cached plan.");
}

} // namespace

int main()
{
    try
    {
#ifdef NDEBUG
        std::cout << "STAMP22_BUILD release\n";
#else
        std::cout << "STAMP22_BUILD debug\n";
#endif
        TestHardLimitsRejectBeforeProportionalWork();
        TestColdFormatDecoding();
        TestBoundedWarmAssetCache();
        TestCatalogueQueriesNeverScanSources();
        TestSparseDensePlanningAndLargePreview();
        TestUnchangedPreviewPerformsNoProportionalWork();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
