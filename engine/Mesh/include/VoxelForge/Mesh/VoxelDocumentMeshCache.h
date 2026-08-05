#pragma once

#include "VoxelChunkGrid.h"
#include "VoxelMeshBuilder.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

// VF-0262 (lot 262-3): incremental mesh cache. The sub-model is partitioned
// into fixed chunks; Synchronize consumes VoxelDocument::ChangesSince to
// rebuild only the touched chunks (plus the axis neighbours of chunk-border
// positions) and reassembles the single mesh consumers read via Mesh().
// Palette-only revisions adopt the new revision without any remesh. Any case
// the journal cannot answer (evicted history, overflowed delta, identity or
// model change, too many touched chunks) falls back to the full chunked
// rebuild. The assembled mesh carries exactly the faces of a full build
// (order may differ); the global face limit is enforced identically.
class VoxelDocumentMeshCache final
{
public:
    // VF-0265 (lot 1): the chunk partition now lives in VoxelChunkGrid.h so the
    // incremental preview composer derives exactly the same chunks. These
    // aliases keep every existing caller compiling unchanged.
    static constexpr std::int32_t ChunkEdgeLength = VoxelChunkEdgeLength;
    static constexpr std::size_t MaximumIncrementalChunkRebuilds = 64U;

    // VF-0262 (lot 262-4a): chunk identity, public so renderers can maintain
    // per-chunk GPU buffers keyed on it.
    using ChunkKey = VoxelChunkKey;

    [[nodiscard]] VoxelDocumentMeshSyncResult Synchronize(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentIdentity,
        std::size_t modelIndex = 0U);
    void Clear() noexcept;

    [[nodiscard]] bool HasMesh() const noexcept;
    // VF-0262 (lot 262-4c): the assembled mesh is built lazily on first
    // access after a rebuild (statistics-only consumers should prefer the
    // Total*Count accessors, which sum the chunks without assembling).
    // Returns nullptr when the cache holds no state or assembly failed to
    // allocate; retried on the next call.
    [[nodiscard]] const MeshData* Mesh() const noexcept;
    [[nodiscard]] std::size_t TotalVertexCount() const noexcept;
    [[nodiscard]] std::size_t TotalTriangleCount() const noexcept;
    [[nodiscard]] std::size_t TotalFaceCount() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> DocumentRevision() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> DocumentIdentity() const noexcept;
    [[nodiscard]] std::size_t ModelIndex() const noexcept;
    [[nodiscard]] std::size_t BuildCount() const noexcept;

    // VF-0262 (lot 262-3) diagnostics.
    [[nodiscard]] std::size_t FullRebuildCount() const noexcept;
    [[nodiscard]] std::size_t IncrementalRebuildCount() const noexcept;
    [[nodiscard]] std::size_t LastRebuildChunkCount() const noexcept;
    [[nodiscard]] std::size_t ChunkCount() const noexcept;

    // VF-0262 (lot 262-4a): per-chunk consumption for partial GPU uploads.
    // Chunks() only contains non-empty chunk meshes. LastSyncTouchedChunks()
    // lists the keys rebuilt (or emptied and erased) by the last successful
    // Synchronize; it is empty after an Unchanged result (fast path or
    // palette-only revision). LastSyncWasFullRebuild() tells consumers to
    // refresh everything instead of patching the touched keys.
    [[nodiscard]] const std::map<ChunkKey, MeshData>& Chunks() const noexcept;
    [[nodiscard]] const std::vector<ChunkKey>&
    LastSyncTouchedChunks() const noexcept;
    [[nodiscard]] bool LastSyncWasFullRebuild() const noexcept;

private:
    [[nodiscard]] static ChunkKey KeyForPosition(
        const Asset::Voxel::VoxelPosition& position) noexcept;

    [[nodiscard]] VoxelDocumentMeshSyncResult RebuildAllChunks(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t revision,
        std::uint64_t documentIdentity,
        std::size_t modelIndex);
    [[nodiscard]] VoxelDocumentMeshSyncResult RebuildChunks(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t revision,
        const std::vector<ChunkKey>& keys,
        std::size_t modelIndex);
    [[nodiscard]] bool AssembleMesh() const;

    std::map<ChunkKey, MeshData> chunkMeshes_;
    std::vector<ChunkKey> lastSyncTouchedChunks_;
    bool lastSyncWasFullRebuild_ = false;
    // Lazy assembly state (262-4c): mutable because Mesh() is conceptually
    // const — it only materializes the aggregation of the chunk meshes.
    mutable std::optional<MeshData> mesh_;
    mutable bool meshDirty_ = false;
    bool hasState_ = false;
    std::optional<std::uint64_t> documentRevision_;
    std::optional<std::uint64_t> documentIdentity_;
    std::size_t modelIndex_ = 0U;
    std::size_t buildCount_ = 0U;
    std::size_t fullRebuildCount_ = 0U;
    std::size_t incrementalRebuildCount_ = 0U;
    std::size_t lastRebuildChunkCount_ = 0U;
};

} // namespace VoxelForge::Mesh
