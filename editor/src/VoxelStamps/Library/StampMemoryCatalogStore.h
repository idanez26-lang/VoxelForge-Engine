#pragma once

#include "VoxelStamps/Library/IStampCatalogStore.h"

#include <optional>

namespace VoxelForge::Editor::Stamps
{

/// Session-local derived catalogue used by My Library. The .vfstamp files
/// remain authoritative and the catalogue is rebuilt after every application
/// restart, so no profile path or derived absolute path is persisted.
class StampMemoryCatalogStore final : public IStampCatalogStore
{
public:
    [[nodiscard]] StampCatalogResult LoadCatalogue() const override
    {
        return catalogue_
            ? StampCatalogResult{.Catalog = *catalogue_}
            : StampCatalogResult{
                  .Error = StampCatalogError::Missing,
                  .Message = "My Library catalogue has not been built yet."};
    }

    [[nodiscard]] StampCatalogResult WriteCatalogueAtomically(
        const StampCatalog& catalogue) override
    {
        catalogue_ = catalogue;
        return {.Catalog = *catalogue_};
    }

    void Clear() noexcept { catalogue_.reset(); }

private:
    std::optional<StampCatalog> catalogue_;
};

} // namespace VoxelForge::Editor::Stamps
