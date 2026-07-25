#pragma once

#include "VoxelStamps/Library/IStampCatalogStore.h"
#include "VoxelStamps/Library/IStampLibraryRepository.h"

namespace VoxelForge::Editor::Stamps
{

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

private:
    [[nodiscard]] StampCatalogResult LoadCachedCatalogue();
    void StoreCache(const StampCatalog& catalogue);

    IStampLibraryRepository& sources_;
    IStampCatalogStore& store_;
    StampCatalog cachedCatalogue_;
    bool cacheValid_ = false;
};

} // namespace VoxelForge::Editor::Stamps
