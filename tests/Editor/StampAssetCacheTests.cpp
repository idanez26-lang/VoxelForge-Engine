#include "VoxelStamps/Library/StampAssetCache.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Library/StampUserLibraryRepository.h"

#include "VoxelForge/Core/UserDataPaths.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;
namespace fs = std::filesystem;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryLibraries final
{
  public:
    TemporaryLibraries()
        : root_(
              fs::temp_directory_path() / ("VoxelForgeStampAssetCache-" +
                                           std::to_string(
                                               std::chrono::steady_clock::now()
                                                   .time_since_epoch()
                                                   .count()))),
          projectRoot_(root_ / "Project"),
          localDataRoot_(root_ / "LocalAppData"),
          user_(Core::UserDataPaths({}, localDataRoot_))
    {
        Require(
            fs::create_directories(projectRoot_ / "Assets"),
            "Unable to create the isolated project root.");
        Require(
            fs::create_directories(localDataRoot_),
            "Unable to create the isolated user-data root.");
        Require(
            project_.SetProjectRoot(projectRoot_),
            "Unable to configure the isolated Project Library.");
        Require(
            !user_.UserDataRoot().empty(),
            "Unable to configure the isolated My Library.");
    }

    ~TemporaryLibraries()
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] StampProjectLibraryRepository& Project() noexcept
    {
        return project_;
    }

    [[nodiscard]] StampUserLibraryRepository& User() noexcept
    {
        return user_;
    }

    [[nodiscard]] fs::path ProjectSource(
        const StampAssetReference& reference) const
    {
        return projectRoot_ / reference.RelativePath;
    }

  private:
    fs::path root_;
    fs::path projectRoot_;
    fs::path localDataRoot_;
    StampProjectLibraryRepository project_;
    StampUserLibraryRepository user_;
};

VoxelStamp MakeStamp(
    const std::uint64_t id,
    const std::uint32_t width,
    const std::uint8_t red)
{
    std::vector<StampVoxel> voxels;
    voxels.reserve(width);
    for (std::uint32_t x = 0U; x < width; ++x)
    {
        voxels.push_back(
            {.Position = {.X = static_cast<std::int32_t>(x), .Y = 0, .Z = 0},
             .LocalColorId = 0U});
    }

    StampValidationResult validation{};
    auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = {}},
        {.Minimum = {},
         .Maximum =
             {.X = static_cast<std::int32_t>(width - 1U), .Y = 0, .Z = 0},
         .Dimensions = {.X = width, .Y = 1U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Auto,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition =
             {.X = static_cast<std::int32_t>(
                  width * StampFixedPoint::UnitsPerVoxel / 2U),
              .Y = StampFixedPoint::UnitsPerVoxel / 2,
              .Z = StampFixedPoint::UnitsPerVoxel / 2},
         .AutoPolicyVersion = 1U},
        {},
        {{.LocalColorId = 0U,
          .Color = {.Red = red, .Green = 20U, .Blue = 30U, .Alpha = 255U}}},
        std::move(voxels), DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value(), validation.Message);
    return std::move(*stamp);
}

StampAssetReference Install(
    IStampLibraryRepository& repository,
    VoxelStamp stamp,
    const std::string_view fileStem,
    const bool replace = false)
{
    const StampLibraryResult result = repository.Install(
        stamp, {.PreferredFileStem = std::string(fileStem),
                .ReplaceExisting = replace});
    Require(result.Succeeded(), result.Message);
    return result.Reference;
}

void TestLazyHitAndExplicitInvalidation()
{
    TemporaryLibraries libraries;
    const StampAssetReference reference =
        Install(libraries.Project(), MakeStamp(1001U, 2U, 10U), "lazy-hit");
    StampAssetCache cache(libraries.Project(), libraries.User());

    const StampAssetCacheResult first = cache.GetOrLoad(reference);
    const StampAssetCacheResult second = cache.GetOrLoad(reference);
    const StampAssetCacheMetrics warm = cache.Metrics();
    Require(
        first.Succeeded() && !first.CacheHit && second.Succeeded() &&
            second.CacheHit && first.Stamp == second.Stamp &&
            warm.Misses == 1U && warm.Hits == 1U && warm.LoadAttempts == 1U &&
            warm.Loads == 1U && warm.Entries == 1U,
        "An unchanged Stamp must parse once and return the retained instance.");

    Require(
        cache.Invalidate(reference) && !cache.Invalidate(reference),
        "Explicit invalidation must remove exactly one cached reference.");
    const StampAssetCacheResult reloaded = cache.GetOrLoad(reference);
    Require(
        reloaded.Succeeded() && !reloaded.CacheHit &&
            cache.Metrics().LoadAttempts == 2U,
        "An invalidated Stamp must be parsed again on demand.");
}

void TestSourceModificationAndCorruptionNeverReturnStaleData()
{
    TemporaryLibraries libraries;
    const StampAssetReference original =
        Install(libraries.Project(), MakeStamp(1002U, 1U, 20U), "changing");
    StampAssetCache cache(libraries.Project(), libraries.User());
    Require(
        cache.GetOrLoad(original).Succeeded(),
        "Unable to prime the source-change fixture.");

    const StampAssetReference replacement = Install(
        libraries.Project(), MakeStamp(1002U, 5U, 40U), "changing", true);
    const StampAssetCacheResult stale = cache.GetOrLoad(original);
    Require(
        !stale.Succeeded() &&
            stale.Error == StampLibraryError::InvalidReference &&
            cache.Metrics().SourceChanges >= 1U,
        "A modified source must invalidate the cached revision before reload.");

    const StampAssetCacheResult current = cache.GetOrLoad(replacement);
    Require(
        current.Succeeded() && current.Stamp->Voxels().size() == 5U,
        "The replacement source must load after the stale revision is "
        "rejected.");

    {
        std::ofstream corrupt(
            libraries.ProjectSource(replacement),
            std::ios::binary | std::ios::trunc);
        corrupt << "corrupt";
        Require(
            static_cast<bool>(corrupt),
            "Unable to corrupt the isolated Stamp fixture.");
    }
    const StampAssetCacheResult corrupted = cache.GetOrLoad(replacement);
    Require(
        !corrupted.Succeeded() &&
            corrupted.Error == StampLibraryError::InvalidAsset &&
            corrupted.Stamp == nullptr,
        "A corrupt changed source must never return its previously cached "
        "Stamp.");
}

void TestMemoryBudgetAndDeterministicLruEviction()
{
    TemporaryLibraries libraries;
    const StampAssetReference first =
        Install(libraries.Project(), MakeStamp(1003U, 3U, 30U), "first");
    const StampAssetReference second =
        Install(libraries.Project(), MakeStamp(1004U, 3U, 40U), "second");
    const StampAssetReference third =
        Install(libraries.Project(), MakeStamp(1005U, 3U, 50U), "third");

    StampAssetCache probe(libraries.Project(), libraries.User());
    const StampAssetCacheResult measured = probe.GetOrLoad(first);
    Require(measured.Succeeded(), "Unable to measure a cached Stamp fixture.");
    const std::size_t oneEntryBytes = measured.Stamp->RetainedBytes();

    StampAssetCache cache(
        libraries.Project(), libraries.User(),
        {.MemoryBudgetBytes = oneEntryBytes * 2U});
    Require(
        cache.GetOrLoad(first).Succeeded() &&
            cache.GetOrLoad(second).Succeeded() &&
            cache.GetOrLoad(first).CacheHit &&
            cache.GetOrLoad(third).Succeeded(),
        "Unable to populate the deterministic LRU fixture.");

    const StampAssetCacheMetrics afterEviction = cache.Metrics();
    Require(
        afterEviction.Entries == 2U && afterEviction.Evictions == 1U &&
            afterEviction.RetainedBytes <= afterEviction.MemoryBudgetBytes,
        "The LRU cache must remain inside its configured memory budget.");
    Require(
        cache.GetOrLoad(first).CacheHit && !cache.GetOrLoad(second).CacheHit,
        "The least recently used Stamp must be evicted deterministically.");

    StampAssetCache oversized(
        libraries.Project(), libraries.User(),
        {.MemoryBudgetBytes = oneEntryBytes - 1U});
    Require(
        oversized.GetOrLoad(first).Succeeded() &&
            !oversized.GetOrLoad(first).CacheHit &&
            oversized.Metrics().Entries == 0U &&
            oversized.Metrics().OversizedLoads == 2U,
        "An oversized Stamp may load but must never be retained above budget.");
}

void TestScopeInvalidationForProjectSwitch()
{
    TemporaryLibraries libraries;
    const StampAssetReference project =
        Install(libraries.Project(), MakeStamp(1006U, 2U, 60U), "project");
    const StampAssetReference user =
        Install(libraries.User(), MakeStamp(1007U, 2U, 70U), "user");
    StampAssetCache cache(libraries.Project(), libraries.User());

    Require(
        cache.GetOrLoad(project).Succeeded() &&
            cache.GetOrLoad(user).Succeeded() && cache.Metrics().Entries == 2U,
        "Unable to populate both scoped caches.");
    Require(
        cache.InvalidateScope(StampLibraryScope::Project) == 1U &&
            cache.Metrics().Entries == 1U,
        "A project switch must invalidate only Project Library entries.");
    Require(
        cache.GetOrLoad(user).CacheHit && !cache.GetOrLoad(project).CacheHit,
        "My Library must survive project-scoped invalidation.");

    Require(
        libraries.Project().Remove(project).Succeeded(),
        "Unable to remove the isolated source asset.");
    const StampAssetCacheResult missing = cache.GetOrLoad(project);
    Require(
        !missing.Succeeded() &&
            missing.Error == StampLibraryError::AssetNotFound,
        "A removed source must not be returned from the cache.");
}

} // namespace

int main()
{
    try
    {
        TestLazyHitAndExplicitInvalidation();
        TestSourceModificationAndCorruptionNeverReturnStaleData();
        TestMemoryBudgetAndDeterministicLruEviction();
        TestScopeInvalidationForProjectSwitch();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
