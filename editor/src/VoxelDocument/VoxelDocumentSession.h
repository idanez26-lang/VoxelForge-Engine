#pragma once

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

enum class VoxelDocumentSessionError
{
    None,
    NoProject,
    ExternalAsset,
    InvalidAsset,
    DocumentLoadFailed
};

struct VoxelDocumentSessionResult final
{
    VoxelDocumentSessionError Error = VoxelDocumentSessionError::None;
    Asset::Voxel::VoxelDocumentError DocumentError =
        Asset::Voxel::VoxelDocumentError::None;
    std::string Message;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == VoxelDocumentSessionError::None;
    }
};

class VoxelDocumentSession final
{
public:
    [[nodiscard]] bool SetProjectRoot(
        const std::filesystem::path& projectRoot);
    void ClearProject() noexcept;
    [[nodiscard]] VoxelDocumentSessionResult Open(
        const std::filesystem::path& internalAssetPath,
        std::optional<std::string> assetId = std::nullopt);
    [[nodiscard]] VoxelDocumentSessionResult OpenInspected(
        const std::filesystem::path& internalAssetPath,
        const Asset::Vox::VoxModel& inspectedModel,
        std::optional<std::string> assetId = std::nullopt);
    void Close() noexcept;

    [[nodiscard]] bool HasActiveDocument() const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelDocument* ActiveDocument() noexcept;
    [[nodiscard]] const Asset::Voxel::VoxelDocument* ActiveDocument()
        const noexcept;
    [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept;
    [[nodiscard]] bool IsDirty() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;

private:
    [[nodiscard]] VoxelDocumentSessionResult ResolveInternalAsset(
        const std::filesystem::path& candidate,
        std::filesystem::path& resolved) const;
    [[nodiscard]] VoxelDocumentSessionResult Adopt(
        Asset::Voxel::VoxDocumentLoadResult loaded);

    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
    std::unique_ptr<Asset::Voxel::VoxelDocument> activeDocument_;
    std::uint64_t generation_ = 0U;
};

} // namespace VoxelForge::Editor
