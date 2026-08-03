#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace VoxelForge::Mesh
{

VoxelDocumentMeshCache::ChunkKey VoxelDocumentMeshCache::KeyForPosition(
    const Asset::Voxel::VoxelPosition& position) noexcept
{
    const auto floorDivide = [](const std::int32_t value) noexcept
    {
        const std::int32_t quotient = value / ChunkEdgeLength;
        return (value % ChunkEdgeLength < 0) ? quotient - 1 : quotient;
    };
    return {
        floorDivide(position.X),
        floorDivide(position.Y),
        floorDivide(position.Z)};
}

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

    // VF-0262 (lot 262-3): incremental path — same document, same sub-model,
    // and the revision journal can name every touched position since the
    // cached revision.
    if (mesh_ && documentIdentity_ == documentIdentity &&
        modelIndex_ == modelIndex && documentRevision_)
    {
        const std::optional<std::vector<
            Asset::Voxel::VoxelDocument::TouchedPosition>>
            changes = document.ChangesSince(*documentRevision_);
        if (changes)
        {
            if (changes->empty())
            {
                // Palette-only revisions: vertices carry palette indices,
                // so the geometry is untouched. Adopt the revision, keep
                // the mesh.
                documentRevision_ = revision;
                return {
                    true,
                    VoxelDocumentMeshSyncStatus::Unchanged,
                    MeshBuildError::None,
                    "Voxel document mesh unaffected by palette-only changes."};
            }
            try
            {
                std::vector<ChunkKey> keys;
                keys.reserve(changes->size() * 2U);
                for (const Asset::Voxel::VoxelDocument::TouchedPosition&
                         touched : *changes)
                {
                    const Asset::Voxel::VoxelPosition& position =
                        touched.Position;
                    const ChunkKey key = KeyForPosition(position);
                    keys.push_back(key);
                    // VF-0262 (262-3bis): only occupancy changes can flip
                    // face visibility inside the axis neighbour; a pure
                    // recolor never invalidates neighbours. Face rules only
                    // look at 6-neighbours, so axis neighbours suffice (no
                    // diagonals).
                    if (!touched.OccupancyChanged) continue;
                    const std::int32_t localX =
                        position.X - key.X * ChunkEdgeLength;
                    const std::int32_t localY =
                        position.Y - key.Y * ChunkEdgeLength;
                    const std::int32_t localZ =
                        position.Z - key.Z * ChunkEdgeLength;
                    if (localX == 0)
                        keys.push_back({key.X - 1, key.Y, key.Z});
                    else if (localX == ChunkEdgeLength - 1)
                        keys.push_back({key.X + 1, key.Y, key.Z});
                    if (localY == 0)
                        keys.push_back({key.X, key.Y - 1, key.Z});
                    else if (localY == ChunkEdgeLength - 1)
                        keys.push_back({key.X, key.Y + 1, key.Z});
                    if (localZ == 0)
                        keys.push_back({key.X, key.Y, key.Z - 1});
                    else if (localZ == ChunkEdgeLength - 1)
                        keys.push_back({key.X, key.Y, key.Z + 1});
                }
                std::sort(keys.begin(), keys.end());
                keys.erase(
                    std::unique(keys.begin(), keys.end()), keys.end());
                if (keys.size() <= MaximumIncrementalChunkRebuilds)
                    return RebuildChunks(
                        document, revision, keys, modelIndex);
            }
            catch (const std::bad_alloc&)
            {
                // Fall through to the full rebuild, which owns its own
                // allocation error handling.
            }
        }
    }

    return RebuildAllChunks(document, revision, documentIdentity, modelIndex);
}

VoxelDocumentMeshSyncResult VoxelDocumentMeshCache::RebuildChunks(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t revision,
    const std::vector<ChunkKey>& keys,
    const std::size_t modelIndex)
{
    // Build every touched chunk before committing anything: a failure leaves
    // the cache exactly as it was (same guarantee as the full rebuild path).
    std::vector<std::pair<ChunkKey, MeshData>> rebuilt;
    try
    {
        rebuilt.reserve(keys.size());
    }
    catch (const std::bad_alloc&)
    {
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::AllocationFailure,
            "Unable to allocate the incremental chunk rebuild buffer."};
    }
    for (const ChunkKey& key : keys)
    {
        const Asset::Voxel::VoxelPosition minimum{
            key.X * ChunkEdgeLength,
            key.Y * ChunkEdgeLength,
            key.Z * ChunkEdgeLength};
        const Asset::Voxel::VoxelPosition maximum{
            minimum.X + ChunkEdgeLength - 1,
            minimum.Y + ChunkEdgeLength - 1,
            minimum.Z + ChunkEdgeLength - 1};
        MeshBuildResult built =
            VoxelMeshBuilder::Build(document, minimum, maximum, modelIndex);
        if (!built.Succeeded || !built.Mesh)
        {
            return {
                false,
                VoxelDocumentMeshSyncStatus::Unchanged,
                built.Error,
                built.Message};
        }
        rebuilt.emplace_back(key, std::move(*built.Mesh));
    }

    // Enforce the global face limit exactly like a full build would.
    std::size_t totalFaces = 0U;
    for (const auto& [key, mesh] : chunkMeshes_)
    {
        if (!std::binary_search(keys.begin(), keys.end(), key))
            totalFaces += mesh.FaceCount();
    }
    for (const auto& entry : rebuilt) totalFaces += entry.second.FaceCount();
    if (totalFaces > VoxelMeshBuilder::MaximumFaceCount)
    {
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::TooLarge,
            "Visible voxel document mesh exceeds the v1 face limit."};
    }

    try
    {
        for (auto& [key, mesh] : rebuilt)
        {
            if (mesh.Empty()) chunkMeshes_.erase(key);
            else chunkMeshes_.insert_or_assign(key, std::move(mesh));
        }
    }
    catch (const std::bad_alloc&)
    {
        // The stale cached revision forces the next Synchronize to rebuild
        // the same chunks again, repairing any partial commit.
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::AllocationFailure,
            "Unable to allocate the chunk mesh storage."};
    }
    if (!AssembleMesh())
    {
        // Chunks are already consistent with the document; the stale
        // revision forces the next Synchronize to retry the assembly.
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::AllocationFailure,
            "Unable to allocate CPU memory for the assembled voxel mesh."};
    }
    documentRevision_ = revision;
    ++buildCount_;
    ++incrementalRebuildCount_;
    lastRebuildChunkCount_ = keys.size();
    return {
        true,
        VoxelDocumentMeshSyncStatus::Rebuilt,
        MeshBuildError::None,
        "Voxel document mesh rebuilt incrementally."};
}

VoxelDocumentMeshSyncResult VoxelDocumentMeshCache::RebuildAllChunks(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t revision,
    const std::uint64_t documentIdentity,
    const std::size_t modelIndex)
{
    // Mirror the edge-case semantics of VoxelMeshBuilder::Build(document).
    const bool emptyDocument =
        document.GetModelCount() == 0U && modelIndex == 0U;
    const Asset::Voxel::VoxelSubModel* model =
        emptyDocument ? nullptr : document.GetModel(modelIndex);
    if (!emptyDocument && model == nullptr)
    {
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::InvalidSource,
            "Voxel document sub-model index is invalid."};
    }

    std::map<ChunkKey, MeshData> chunks;
    if (model != nullptr && model->Bounds().HasValue)
    {
        const ChunkKey minimumKey = KeyForPosition(model->Bounds().Minimum);
        const ChunkKey maximumKey = KeyForPosition(model->Bounds().Maximum);
        std::size_t totalFaces = 0U;
        for (std::int32_t z = minimumKey.Z; z <= maximumKey.Z; ++z)
        {
            for (std::int32_t y = minimumKey.Y; y <= maximumKey.Y; ++y)
            {
                for (std::int32_t x = minimumKey.X; x <= maximumKey.X; ++x)
                {
                    const Asset::Voxel::VoxelPosition minimum{
                        x * ChunkEdgeLength,
                        y * ChunkEdgeLength,
                        z * ChunkEdgeLength};
                    const Asset::Voxel::VoxelPosition maximum{
                        minimum.X + ChunkEdgeLength - 1,
                        minimum.Y + ChunkEdgeLength - 1,
                        minimum.Z + ChunkEdgeLength - 1};
                    MeshBuildResult built = VoxelMeshBuilder::Build(
                        document, minimum, maximum, modelIndex);
                    if (!built.Succeeded || !built.Mesh)
                    {
                        return {
                            false,
                            VoxelDocumentMeshSyncStatus::Unchanged,
                            built.Error,
                            built.Message};
                    }
                    if (built.Mesh->Empty()) continue;
                    totalFaces += built.Mesh->FaceCount();
                    if (totalFaces > VoxelMeshBuilder::MaximumFaceCount)
                    {
                        return {
                            false,
                            VoxelDocumentMeshSyncStatus::Unchanged,
                            MeshBuildError::TooLarge,
                            "Visible voxel document mesh exceeds the v1 "
                            "face limit."};
                    }
                    try
                    {
                        chunks.emplace(
                            ChunkKey{x, y, z}, std::move(*built.Mesh));
                    }
                    catch (const std::bad_alloc&)
                    {
                        return {
                            false,
                            VoxelDocumentMeshSyncStatus::Unchanged,
                            MeshBuildError::AllocationFailure,
                            "Unable to allocate the chunk mesh storage."};
                    }
                }
            }
        }
    }

    chunkMeshes_ = std::move(chunks);
    if (!AssembleMesh())
    {
        return {
            false,
            VoxelDocumentMeshSyncStatus::Unchanged,
            MeshBuildError::AllocationFailure,
            "Unable to allocate CPU memory for the assembled voxel mesh."};
    }
    documentIdentity_ = documentIdentity;
    documentRevision_ = revision;
    modelIndex_ = modelIndex;
    ++buildCount_;
    ++fullRebuildCount_;
    lastRebuildChunkCount_ = chunkMeshes_.size();
    return {
        true,
        VoxelDocumentMeshSyncStatus::Rebuilt,
        MeshBuildError::None,
        "Voxel document mesh rebuilt."};
}

bool VoxelDocumentMeshCache::AssembleMesh()
{
    try
    {
        std::size_t vertexCount = 0U;
        std::size_t indexCount = 0U;
        for (const auto& entry : chunkMeshes_)
        {
            vertexCount += entry.second.VertexCount();
            indexCount += entry.second.IndexCount();
        }
        MeshData assembled;
        assembled.Reserve(vertexCount, indexCount);
        for (const auto& entry : chunkMeshes_)
            assembled.Append(entry.second);
        mesh_ = std::move(assembled);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

void VoxelDocumentMeshCache::Clear() noexcept
{
    chunkMeshes_.clear();
    mesh_.reset();
    documentRevision_.reset();
    documentIdentity_.reset();
    modelIndex_ = 0U;
    lastRebuildChunkCount_ = 0U;
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

std::size_t VoxelDocumentMeshCache::FullRebuildCount() const noexcept
{
    return fullRebuildCount_;
}

std::size_t VoxelDocumentMeshCache::IncrementalRebuildCount() const noexcept
{
    return incrementalRebuildCount_;
}

std::size_t VoxelDocumentMeshCache::LastRebuildChunkCount() const noexcept
{
    return lastRebuildChunkCount_;
}

std::size_t VoxelDocumentMeshCache::ChunkCount() const noexcept
{
    return chunkMeshes_.size();
}

} // namespace VoxelForge::Mesh
