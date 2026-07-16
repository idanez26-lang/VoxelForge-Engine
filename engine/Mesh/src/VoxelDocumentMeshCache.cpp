#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"

#include <utility>

namespace VoxelForge::Mesh
{

VoxelDocumentMeshSyncResult VoxelDocumentMeshCache::Synchronize(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentIdentity,
    const std::size_t modelIndex)
{
    const std::uint64_t revision = document.GetRevision();
    if (mesh_ && documentIdentity_ == documentIdentity &&
        documentRevision_ == revision && modelIndex_ == modelIndex)
    {
        return {
            true,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::None,
            "Voxel document mesh is already synchronized."};
    }

    MeshBuildResult built = VoxelMeshBuilder::Build(document, modelIndex);
    if (!built.Succeeded || !built.Mesh)
    {
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            built.Error,
            built.Message};
    }

    mesh_ = std::move(*built.Mesh);
    documentIdentity_ = documentIdentity;
    documentRevision_ = revision;
    modelIndex_ = modelIndex;
    ++buildCount_;
    return {
        true,
        VoxelDocumentMeshSyncStatus::Rebuilt,
        MeshBuildError::None,
        "Voxel document mesh rebuilt."};
}

void VoxelDocumentMeshCache::Clear() noexcept
{
    mesh_.reset();
    documentRevision_.reset();
    documentIdentity_.reset();
    modelIndex_ = 0U;
}

bool VoxelDocumentMeshCache::HasMesh() const noexcept
{
    return mesh_.has_value();
}

const MeshData* VoxelDocumentMeshCache::Mesh() const noexcept
{
    return mesh_ ? &*mesh_ : nullptr;
}

std::optional<std::uint64_t>
VoxelDocumentMeshCache::DocumentRevision() const noexcept
{
    return documentRevision_;
}

std::optional<std::uint64_t>
VoxelDocumentMeshCache::DocumentIdentity() const noexcept
{
    return documentIdentity_;
}

std::size_t VoxelDocumentMeshCache::ModelIndex() const noexcept
{
    return modelIndex_;
}

std::size_t VoxelDocumentMeshCache::BuildCount() const noexcept
{
    return buildCount_;
}

} // namespace VoxelForge::Mesh
