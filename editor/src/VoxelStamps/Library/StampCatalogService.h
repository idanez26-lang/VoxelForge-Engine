#pragma once

#include "VoxelStamps/Library/IStampCatalogStore.h"
#include "VoxelStamps/Library/IStampLibraryRepository.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

struct StampCatalogServiceMetrics final
{
    std::uint64_t Queries = 0U;
    std::uint64_t CacheHits = 0U;
    std::uint64_t StoreLoads = 0U;
    std::uint64_t Rebuilds = 0U;
    std::uint64_t SourceInventoryScans = 0U;
    std::uint64_t SourceReads = 0U;
    std::uint64_t CacheInvalidations = 0U;
    std::size_t SearchIndexEntries = 0U;
    std::size_t SearchIndexBytes = 0U;
};

/// Application service for the derived catalogue. It only asks the repository
/// for a validated source inventory and individual assets; it never scans the
/// filesystem itself and never changes source .vfstamp files.
class StampCatalogService final
{
public:
    StampCatalogService(IStampLibraryRepository& sources, IStampCatalogStore& store);

    [[nodiscard]] StampCatalogResult RebuildCatalogue();
    [[nodiscard]] StampCatalogResult Query(const StampCatalogQuery& query);
    [[nodiscard]] StampCatalogResult UpsertDerivedEntry(const StampCatalogEntry& entry);
    [[nodiscard]] StampCatalogResult RemoveDerivedEntry(const Core::UUID& id);
    void InvalidateCache() noexcept;
    [[nodiscard]] StampCatalogServiceMetrics Metrics() const noexcept;
    void ResetMetrics() noexcept;

private:
    [[nodiscard]] StampCatalogResult LoadCachedCatalogue();
    void StoreCache(const StampCatalog& catalogue);

    IStampLibraryRepository& sources_;
    IStampCatalogStore& store_;
    StampCatalog cachedCatalogue_;
    std::vector<std::string> cachedSearchText_;
    std::size_t cachedSearchBytes_ = 0U;
    bool cacheValid_ = false;
    StampCatalogServiceMetrics metrics_{};
};

} // namespace VoxelForge::Editor::Stamps
