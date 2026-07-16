#pragma once

#include "VoxelDocument.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace VoxelForge::Asset::Voxel
{

struct VoxDocumentLoadResult final
{
    VoxelDocumentError Error = VoxelDocumentError::None;
    std::string Message;
    std::vector<std::string> Warnings;
    std::optional<VoxelDocument> Document;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == VoxelDocumentError::None && Document.has_value();
    }
};

class VoxDocumentLoader final
{
public:
    [[nodiscard]] VoxDocumentLoadResult Load(
        const std::filesystem::path& sourcePath,
        std::optional<std::string> assetId = std::nullopt) const;
    [[nodiscard]] VoxDocumentLoadResult Build(
        const Vox::VoxModel& source,
        const std::filesystem::path& sourcePath,
        std::optional<std::string> assetId = std::nullopt) const;
};

} // namespace VoxelForge::Asset::Voxel
