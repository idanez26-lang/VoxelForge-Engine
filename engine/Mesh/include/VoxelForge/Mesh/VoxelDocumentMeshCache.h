#pragma once

#include "VoxelMeshBuilder.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Mesh
{

enum class VoxelDocumentMeshSyncStatus
{
    Rebuilt,
    Unchanged
};

struct VoxelDocumentMeshSyncResult final
{
    bool Succeeded = false;
    VoxelDocumentMeshSyncStatus Status =
        VoxelDocumentMeshSyncStatus::Unchanged;
    MeshBuildError Error = MeshBuildError::None;
    std::string Message;

    [[nodiscard]] bool Rebuilt() const noexcept
    {
        return Succeeded && Status == VoxelDocumentMeshSyncStatus::Rebuilt;
    }
};

class VoxelDocumentMeshCache final
{
public:
    [[nodiscard]] VoxelDocumentMeshSyncResult Synchronize(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentIdentity,
        std::size_t modelIndex = 0U);
    void Clear() noexcept;

    [[nodiscard]] bool HasMesh() const noexcept;
    [[nodiscard]] const MeshData* Mesh() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> DocumentRevision() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> DocumentIdentity() const noexcept;
    [[nodiscard]] std::size_t ModelIndex() const noexcept;
    [[nodiscard]] std::size_t BuildCount() const noexcept;

private:
    std::optional<MeshData> mesh_;
    std::optional<std::uint64_t> documentRevision_;
    std::optional<std::uint64_t> documentIdentity_;
    std::size_t modelIndex_ = 0U;
    std::size_t buildCount_ = 0U;
};

} // namespace VoxelForge::Mesh
