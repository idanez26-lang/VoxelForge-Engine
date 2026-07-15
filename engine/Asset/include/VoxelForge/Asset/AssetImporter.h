#pragma once

#include "AssetImportResult.h"

#include <optional>

namespace VoxelForge::Asset
{

template<typename AssetType>
struct AssetImportOutcome final
{
    AssetImportResult Result;
    std::optional<AssetType> Asset;
};

} // namespace VoxelForge::Asset
