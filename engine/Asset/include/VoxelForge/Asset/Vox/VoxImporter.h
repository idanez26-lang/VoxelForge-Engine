#pragma once

#include "VoxelForge/Asset/AssetImporter.h"
#include "VoxelForge/Asset/Vox/VoxModel.h"

#include <filesystem>

namespace VoxelForge::Asset::Vox
{

class VoxImporter final
{
public:
    [[nodiscard]] AssetImportOutcome<VoxModel> Inspect(
        const std::filesystem::path& filePath) const;
};

} // namespace VoxelForge::Asset::Vox
