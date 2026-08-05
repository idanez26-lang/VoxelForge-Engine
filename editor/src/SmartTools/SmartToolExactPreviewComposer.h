#pragma once

#include "SmartTools/SmartToolPlan.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Mesh/VoxelChunkGrid.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace VoxelForge::Editor
{
// Renderer-neutral final-state presentation data.  It is composed by applying
// an already resolved immutable plan to a private document copy, then using the
// same VoxelMeshBuilder path as a committed document.  No planner work happens
// here: positions, colours and Before/After values come exclusively from Plan.
// VF-0265 (lot 3e): one chunk of the preview that DIFFERS from the document's
// own chunk. An empty mesh means "this chunk shows nothing while the preview is
// active" — the case of erasing a chunk's last voxel.
struct SmartToolExactPreviewChunk final
{
    Mesh::VoxelChunkKey Key{};
    Mesh::MeshData Mesh;
};

struct SmartToolExactPreviewMesh final
{
    bool Active = false;
    Mesh::MeshData Mesh;
    // VF-0265 (lot 3e): the chunks that differ from the document, and only
    // those. Every chunk absent from this list is displayed exactly as the
    // document's chunk cache describes it — no copy, no upload, no draw change.
    // Populated only on the incremental path; the reference path leaves it
    // empty and fills Mesh alone.
    std::vector<SmartToolExactPreviewChunk> Overrides;
    Voxel::VoxelPalette Palette;
    std::string Error;

    [[nodiscard]] bool Succeeded() const noexcept { return Error.empty(); }
    [[nodiscard]] bool Empty() const noexcept { return Mesh.Empty(); }
};

class SmartToolExactPreviewComposer final
{
public:
    // VF-0265 (lot 3b): incremental source. When DocumentChunks is supplied,
    // composition reuses every chunk the plan does not touch, and for the few
    // it does touch it drops the dirty faces and rebuilds only the dirty box —
    // never the whole 32^3 chunk, which costs 28 ms on a dense document.
    // Left null, composition falls back to the reference path below, so
    // occasional callers (tests, benchmarks) need no change.
    struct Source final
    {
        const Asset::Voxel::VoxelDocument* Document = nullptr;
        // Chunks of the document WITHOUT the plan, at the same revision.
        const std::map<Mesh::VoxelChunkKey, Mesh::MeshData>* DocumentChunks =
            nullptr;
        std::size_t ModelIndex = 0U;
        // VF-0265 (lot 3e): assembling the single mesh is the last cost that
        // still follows the document size — 8 Mo of buffers at a million
        // voxels. A consumer that draws the overrides chunk by chunk sets this
        // to false and pays nothing.
        bool AssembleMesh = true;
    };

    [[nodiscard]] static SmartToolExactPreviewMesh Compose(
        const Asset::Voxel::VoxelDocument& document, const SmartToolPlan& plan);
    // Continuous strokes supply a de-duplicated sequence of planner-produced
    // Before -> After changes. This overload never resolves geometry itself.
    [[nodiscard]] static SmartToolExactPreviewMesh Compose(
        const Asset::Voxel::VoxelDocument& document,
        std::span<const Asset::Voxel::VoxelDocumentChange> changes);

    [[nodiscard]] static SmartToolExactPreviewMesh Compose(
        const Source& source, const SmartToolPlan& plan);
    [[nodiscard]] static SmartToolExactPreviewMesh Compose(
        const Source& source,
        std::span<const Asset::Voxel::VoxelDocumentChange> changes);
};

// One-entry cache keyed by immutable plan identity plus the document identity
// and revision from which its Before state was observed.  It deliberately does
// not inspect the planner or renderer.
class SmartToolExactPreviewCache final
{
public:
    [[nodiscard]] const SmartToolExactPreviewMesh& Resolve(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentIdentity,
        SmartToolPlanPtr plan);
    // VF-0265 (lot 3c): same cache, incremental source. The chunk set takes
    // part in the key: swapping it — model reloaded, cache cleared — must
    // recompose, otherwise the preview would reuse chunks that no longer
    // describe this document.
    [[nodiscard]] const SmartToolExactPreviewMesh& Resolve(
        const SmartToolExactPreviewComposer::Source& source,
        std::uint64_t documentIdentity,
        SmartToolPlanPtr plan);
    void Clear() noexcept;
    [[nodiscard]] std::size_t BuildCount() const noexcept;

private:
    const Asset::Voxel::VoxelDocument* document_ = nullptr;
    std::uint64_t documentIdentity_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    const std::map<Mesh::VoxelChunkKey, Mesh::MeshData>* documentChunks_ =
        nullptr;
    std::size_t modelIndex_ = 0U;
    SmartToolPlanPtr plan_;
    SmartToolExactPreviewMesh mesh_{};
    std::size_t buildCount_ = 0U;
};
} // namespace VoxelForge::Editor
