#pragma once

#include "VoxelMeshBuilder.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
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
    static constexpr std::int32_t ChunkEdgeLength = 32;
    static constexpr std::size_t MaximumIncrementalChunkRebuilds = 64U;

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

    // VF-0262 (lot 262-3) diagnostics.
    [[nodiscard]] std::size_t FullRebuildCount() const noexcept;
    [[nodiscard]] std::size_t IncrementalRebuildCount() const noexcept;
    [[nodiscard]] std::size_t LastRebuildChunkCount() const noexcept;
    [[nodiscard]] std::size_t ChunkCount() const noexcept;

private:
    struct ChunkKey final
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;
        std::int32_t Z = 0;

        [[nodiscard]] bool operator==(const ChunkKey&) const noexcept = default;

        [[nodiscard]] bool operator<(const ChunkKey& other) const noexcept
        {
            return std::tie(X, Y, Z) <
                std::tie(other.X, other.Y, other.Z);
        }
    };

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
    [[nodiscard]] bool AssembleMesh();

    std::map<ChunkKey, MeshData> chunkMeshes_;
    std::optional<MeshData> mesh_;
    std::optional<std::uint64_t> documentRevision_;
    std::optional<std::uint64_t> documentIdentity_;
    std::size_t modelIndex_ = 0U;
    std::size_t buildCount_ = 0U;
    std::size_t fullRebuildCount_ = 0U;
    std::size_t incrementalRebuildCount_ = 0U;
    std::size_t lastRebuildChunkCount_ = 0U;
};

} // namespace VoxelForge::Mesh
