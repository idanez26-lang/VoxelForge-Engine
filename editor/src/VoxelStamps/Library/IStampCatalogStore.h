#pragma once

#include "VoxelStamps/Library/StampCatalogTypes.h"

namespace VoxelForge::Editor::Stamps
{

class IStampCatalogStore
{
public:
    virtual ~IStampCatalogStore() = default;

    [[nodiscard]] virtual StampCatalogResult LoadCatalogue() const = 0;
    [[nodiscard]] virtual StampCatalogResult WriteCatalogueAtomically(
        const StampCatalog& catalogue) = 0;
};

} // namespace VoxelForge::Editor::Stamps
