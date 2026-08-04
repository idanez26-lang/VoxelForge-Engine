#pragma once

#include "VoxelStamps/Library/IStampLibraryRepository.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>

namespace VoxelForge::Editor::Stamps
{

inline constexpr std::size_t DefaultStampAssetCacheBudgetBytes =
    64U * 1024U * 1024U;

struct StampAssetCacheConfiguration final
{
    std::size_t MemoryBudgetBytes = DefaultStampAssetCacheBudgetBytes;
};

struct StampAssetCacheMetrics final
{
    std::uint64_t Hits = 0U;
    std::uint64_t Misses = 0U;
    std::uint64_t LoadAttempts = 0U;
    std::uint64_t Loads = 0U;
    std::uint64_t LoadFailures = 0U;
    std::uint64_t SourceChanges = 0U;
    std::uint64_t Evictions = 0U;
    std::uint64_t Invalidations = 0U;
    std::uint64_t OversizedLoads = 0U;
    std::size_t Entries = 0U;
    std::size_t RetainedBytes = 0U;
    std::size_t MemoryBudgetBytes = 0U;
};

struct StampAssetCacheResult final
{
    StampLibraryError Error = StampLibraryError::None;
    std::string Message{StampLibraryErrorMessage(StampLibraryError::None)};
    StampAssetReference Reference;
    std::shared_ptr<const VoxelStamp> Stamp;
    bool CacheHit = false;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == StampLibraryError::None && Stamp != nullptr;
    }
};

/// CPU-only LRU cache for decoded Stamp source assets. Source metadata is
/// checked before every hit, while the source bytes remain authoritative.
class StampAssetCache final
{
  public:
    explicit StampAssetCache(
        IStampLibraryRepository& projectRepository,
        StampAssetCacheConfiguration configuration = {});
    StampAssetCache(
        IStampLibraryRepository& projectRepository,
        IStampLibraryRepository& userRepository,
        StampAssetCacheConfiguration configuration = {});

    StampAssetCache(const StampAssetCache&) = delete;
    StampAssetCache& operator=(const StampAssetCache&) = delete;

    [[nodiscard]] StampAssetCacheResult GetOrLoad(
        const StampAssetReference& reference);
    [[nodiscard]] bool Invalidate(
        const StampAssetReference& reference) noexcept;
    [[nodiscard]] std::size_t InvalidateScope(StampLibraryScope scope) noexcept;
    void Clear() noexcept;

    [[nodiscard]] StampAssetCacheMetrics Metrics() const noexcept;

  private:
    struct ReferenceHash final
    {
        [[nodiscard]] std::size_t operator()(
            const StampAssetReference& reference) const noexcept;
    };

    struct Entry final
    {
        StampAssetReference Reference;
        StampLibrarySourceFacts SourceFacts;
        std::shared_ptr<const VoxelStamp> Stamp;
        std::size_t RetainedBytes = 0U;
    };

    using Entries = std::list<Entry>;
    using Index = std::
        unordered_map<StampAssetReference, Entries::iterator, ReferenceHash>;

    [[nodiscard]] IStampLibraryRepository& RepositoryFor(
        StampLibraryScope scope) noexcept;
    [[nodiscard]] StampAssetCacheResult LoadStable(
        IStampLibraryRepository& repository,
        const StampAssetReference& reference);
    void Touch(Entries::iterator entry) noexcept;
    void Erase(Entries::iterator entry) noexcept;
    void EraseOtherRevisions(const StampAssetReference& reference) noexcept;
    void EnforceBudget() noexcept;

    IStampLibraryRepository& projectRepository_;
    IStampLibraryRepository& userRepository_;
    StampAssetCacheConfiguration configuration_;
    Entries entries_;
    Index index_;
    StampAssetCacheMetrics metrics_;
    std::size_t retainedBytes_ = 0U;
};

} // namespace VoxelForge::Editor::Stamps
