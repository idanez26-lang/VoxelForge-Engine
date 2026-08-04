#include "VoxelStamps/Library/StampAssetCache.h"

#include <filesystem>
#include <iterator>
#include <new>
#include <string>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] StampAssetCacheResult Failure(
    const StampAssetReference& reference,
    const StampLibraryError error,
    std::string message = {})
{
    return {
        .Error = error,
        .Message = message.empty()
                       ? std::string(StampLibraryErrorMessage(error))
                       : std::move(message),
        .Reference = reference};
}

void HashCombine(std::size_t& value, const std::size_t next) noexcept
{
    value ^= next + 0x9e3779b9U + (value << 6U) + (value >> 2U);
}

} // namespace

StampAssetCache::StampAssetCache(
    IStampLibraryRepository& projectRepository,
    const StampAssetCacheConfiguration configuration)
    : StampAssetCache(projectRepository, projectRepository, configuration)
{
}

StampAssetCache::StampAssetCache(
    IStampLibraryRepository& projectRepository,
    IStampLibraryRepository& userRepository,
    const StampAssetCacheConfiguration configuration)
    : projectRepository_(projectRepository), userRepository_(userRepository),
      configuration_(configuration)
{
}

std::size_t StampAssetCache::ReferenceHash::operator()(
    const StampAssetReference& reference) const noexcept
{
    std::size_t value = std::hash<std::uint64_t>{}(reference.Id.Value());
    HashCombine(value, std::hash<std::string>{}(reference.ContentHash));
    HashCombine(
        value, std::hash<std::filesystem::path>{}(reference.RelativePath));
    HashCombine(
        value, std::hash<unsigned>{}(static_cast<unsigned>(reference.Scope)));
    return value;
}

StampAssetCacheResult StampAssetCache::GetOrLoad(
    const StampAssetReference& reference)
{
    if (reference.Id.Value() == 0U || reference.ContentHash.empty() ||
        reference.RelativePath.empty())
    {
        ++metrics_.Misses;
        ++metrics_.LoadFailures;
        return Failure(
            reference, StampLibraryError::InvalidReference,
            "Cached Stamp references require UUID, content hash, path, and "
            "scope.");
    }

    try
    {
        IStampLibraryRepository& repository = RepositoryFor(reference.Scope);
        const auto cached = index_.find(reference);
        if (cached != index_.end())
        {
            const StampLibrarySourceFactsResult inspected =
                repository.InspectSource(reference);
            if (!inspected.Succeeded())
            {
                Erase(cached->second);
                ++metrics_.Misses;
                ++metrics_.LoadFailures;
                return Failure(reference, inspected.Error, inspected.Message);
            }

            if (*inspected.Facts == cached->second->SourceFacts)
            {
                const std::shared_ptr<const VoxelStamp> stamp =
                    cached->second->Stamp;
                Touch(cached->second);
                ++metrics_.Hits;
                return {
                    .Reference = reference, .Stamp = stamp, .CacheHit = true};
            }

            ++metrics_.SourceChanges;
            Erase(cached->second);
        }

        ++metrics_.Misses;
        return LoadStable(repository, reference);
    }
    catch (const std::bad_alloc&)
    {
        ++metrics_.LoadFailures;
        return Failure(reference, StampLibraryError::AllocationFailure);
    }
}

StampAssetCacheResult StampAssetCache::LoadStable(
    IStampLibraryRepository& repository,
    const StampAssetReference& reference)
{
    for (unsigned attempt = 0U; attempt < 2U; ++attempt)
    {
        const StampLibrarySourceFactsResult before =
            repository.InspectSource(reference);
        if (!before.Succeeded())
        {
            ++metrics_.LoadFailures;
            return Failure(reference, before.Error, before.Message);
        }

        ++metrics_.LoadAttempts;
        StampLibraryResult loaded = repository.Read(reference);
        if (!loaded.Succeeded() || !loaded.Stamp)
        {
            ++metrics_.LoadFailures;
            return Failure(reference, loaded.Error, loaded.Message);
        }

        const StampLibrarySourceFactsResult after =
            repository.InspectSource(reference);
        if (!after.Succeeded())
        {
            ++metrics_.LoadFailures;
            return Failure(reference, after.Error, after.Message);
        }
        if (*before.Facts != *after.Facts)
        {
            ++metrics_.SourceChanges;
            continue;
        }
        if (loaded.Reference != reference)
        {
            ++metrics_.LoadFailures;
            return Failure(
                reference, StampLibraryError::InvalidReference,
                "Loaded Stamp identity does not match its cache reference.");
        }

        auto stamp = std::make_shared<VoxelStamp>(std::move(*loaded.Stamp));
        const std::size_t retainedBytes = stamp->RetainedBytes();
        ++metrics_.Loads;

        if (configuration_.MemoryBudgetBytes == 0U ||
            retainedBytes > configuration_.MemoryBudgetBytes)
        {
            ++metrics_.OversizedLoads;
            return {.Reference = reference, .Stamp = std::move(stamp)};
        }

        EraseOtherRevisions(reference);
        entries_.push_front(
            {.Reference = reference,
             .SourceFacts = *after.Facts,
             .Stamp = stamp,
             .RetainedBytes = retainedBytes});
        try
        {
            index_.emplace(entries_.front().Reference, entries_.begin());
        }
        catch (...)
        {
            entries_.pop_front();
            throw;
        }
        retainedBytes_ += retainedBytes;
        EnforceBudget();
        return {.Reference = reference, .Stamp = std::move(stamp)};
    }

    ++metrics_.LoadFailures;
    return Failure(
        reference, StampLibraryError::IoFailure,
        "Stamp source changed repeatedly while it was being loaded.");
}

bool StampAssetCache::Invalidate(const StampAssetReference& reference) noexcept
{
    const auto cached = index_.find(reference);
    if (cached == index_.end()) return false;
    Erase(cached->second);
    ++metrics_.Invalidations;
    return true;
}

std::size_t StampAssetCache::InvalidateScope(
    const StampLibraryScope scope) noexcept
{
    std::size_t invalidated = 0U;
    for (auto entry = entries_.begin(); entry != entries_.end();)
    {
        if (entry->Reference.Scope != scope)
        {
            ++entry;
            continue;
        }
        const auto removed = entry++;
        Erase(removed);
        ++invalidated;
    }
    metrics_.Invalidations += invalidated;
    return invalidated;
}

void StampAssetCache::Clear() noexcept
{
    const std::size_t invalidated = entries_.size();
    entries_.clear();
    index_.clear();
    retainedBytes_ = 0U;
    metrics_.Invalidations += invalidated;
}

StampAssetCacheMetrics StampAssetCache::Metrics() const noexcept
{
    StampAssetCacheMetrics result = metrics_;
    result.Entries = entries_.size();
    result.RetainedBytes = retainedBytes_;
    result.MemoryBudgetBytes = configuration_.MemoryBudgetBytes;
    return result;
}

IStampLibraryRepository& StampAssetCache::RepositoryFor(
    const StampLibraryScope scope) noexcept
{
    return scope == StampLibraryScope::Project ? projectRepository_
                                               : userRepository_;
}

void StampAssetCache::Touch(const Entries::iterator entry) noexcept
{
    if (entry != entries_.begin())
    {
        entries_.splice(entries_.begin(), entries_, entry);
    }
}

void StampAssetCache::Erase(const Entries::iterator entry) noexcept
{
    retainedBytes_ -= entry->RetainedBytes;
    index_.erase(entry->Reference);
    entries_.erase(entry);
}

void StampAssetCache::EraseOtherRevisions(
    const StampAssetReference& reference) noexcept
{
    for (auto entry = entries_.begin(); entry != entries_.end();)
    {
        const bool sameIdentity = entry->Reference.Scope == reference.Scope &&
                                  entry->Reference.Id == reference.Id;
        if (!sameIdentity || entry->Reference == reference)
        {
            ++entry;
            continue;
        }
        const auto removed = entry++;
        Erase(removed);
        ++metrics_.SourceChanges;
    }
}

void StampAssetCache::EnforceBudget() noexcept
{
    while (retainedBytes_ > configuration_.MemoryBudgetBytes &&
           !entries_.empty())
    {
        const auto leastRecentlyUsed = std::prev(entries_.end());
        Erase(leastRecentlyUsed);
        ++metrics_.Evictions;
    }
}

} // namespace VoxelForge::Editor::Stamps
