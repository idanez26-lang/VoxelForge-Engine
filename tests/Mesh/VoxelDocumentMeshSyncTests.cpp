#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using VoxelForge::Asset::Voxel::VoxelDocument;
using VoxelForge::Asset::Voxel::VoxDocumentLoader;
using VoxelForge::Asset::Vox::DefaultVoxPalette;
using VoxelForge::Asset::Vox::VoxDimensions;
using VoxelForge::Asset::Vox::VoxModel;
using VoxelForge::Asset::Vox::VoxModelMetadata;
using VoxelForge::Asset::Vox::VoxVoxel;
using VoxelForge::Mesh::MeshBuildError;
using VoxelForge::Mesh::VoxelDocumentMeshCache;
using VoxelForge::Mesh::VoxelDocumentMeshSyncStatus;
using VoxelForge::Mesh::VoxelMeshBuilder;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

VoxelDocument Document(std::vector<VoxModelMetadata> models)
{
    VoxModel source;
    source.Version = 150U;
    source.Palette = DefaultVoxPalette();
    source.Models = std::move(models);
    source.DeclaredModelCount = static_cast<std::uint32_t>(source.Models.size());
    source.HasPackChunk = source.Models.size() > 1U;
    auto loaded = VoxDocumentLoader{}.Build(source, "memory.vox");
    Require(loaded.Succeeded(), "Unable to construct document test fixture.");
    return std::move(*loaded.Document);
}

VoxModelMetadata Model(
    const VoxDimensions dimensions,
    std::vector<VoxVoxel> voxels)
{
    return {dimensions, std::move(voxels)};
}

void TestEmptyAndSingleVoxelMesh()
{
    VoxelDocument empty;
    const auto emptyBuild = VoxelMeshBuilder::Build(empty);
    Require(emptyBuild.Succeeded && emptyBuild.Mesh && emptyBuild.Mesh->Empty(),
        "Controlled empty document must produce an empty mesh.");

    VoxelDocument single = Document({
        Model({3U, 4U, 5U}, {{2U, 3U, 4U, 17U}})});
    const auto built = VoxelMeshBuilder::Build(single);
    Require(built.Succeeded && built.Mesh &&
        built.Mesh->FaceCount() == 6U &&
        built.Mesh->VertexCount() == 24U &&
        built.Mesh->TriangleCount() == 12U,
        "Single document voxel geometry is incorrect.");
    for (const auto& vertex : built.Mesh->Vertices())
        Require(vertex.ColorIndex == 17U,
            "Document palette index was not copied to the mesh.");
}

void TestMultiModelAndInvalidIndex()
{
    VoxelDocument document = Document({
        Model({2U, 1U, 1U}, {
            {0U, 0U, 0U, 1U}, {1U, 0U, 0U, 2U}}),
        Model({3U, 1U, 1U}, {
            {0U, 0U, 0U, 3U}, {2U, 0U, 0U, 4U}})});
    const auto primary = VoxelMeshBuilder::Build(document, 0U);
    const auto secondary = VoxelMeshBuilder::Build(document, 1U);
    const auto invalid = VoxelMeshBuilder::Build(document, 2U);
    Require(primary.Succeeded && primary.Mesh &&
        primary.Mesh->FaceCount() == 10U,
        "Adjacent primary sub-model mesh is incorrect.");
    Require(secondary.Succeeded && secondary.Mesh &&
        secondary.Mesh->FaceCount() == 12U,
        "Independent secondary sub-model mesh is incorrect.");
    Require(!invalid.Succeeded && !invalid.Mesh &&
        invalid.Error == MeshBuildError::InvalidSource,
        "Invalid document sub-model index must fail explicitly.");
}

void TestRevisionSynchronization()
{
    VoxelDocument document = Document({
        Model({3U, 1U, 1U}, {{1U, 0U, 0U, 5U}})});
    VoxelDocumentMeshCache cache;

    auto synchronized = cache.Synchronize(document, 10U);
    Require(synchronized.Rebuilt() && cache.HasMesh() &&
        cache.BuildCount() == 1U && cache.DocumentRevision() == 0U &&
        cache.DocumentIdentity() == 10U,
        "Initial document synchronization must build one mesh.");
    const auto* initialMesh = cache.Mesh();
    Require(initialMesh && initialMesh->FaceCount() == 6U,
        "Initial cached mesh is incorrect.");

    synchronized = cache.Synchronize(document, 10U);
    Require(synchronized.Succeeded &&
        synchronized.Status == VoxelDocumentMeshSyncStatus::Unchanged &&
        cache.BuildCount() == 1U && cache.Mesh() == initialMesh,
        "Unchanged revision must not rebuild or replace the cached mesh.");

    Require(document.SetVoxel({0, 0, 0}, 6U).Changed,
        "SetVoxel fixture mutation failed.");
    synchronized = cache.Synchronize(document, 10U);
    Require(synchronized.Rebuilt() && cache.BuildCount() == 2U &&
        cache.DocumentRevision() == 1U && cache.Mesh() &&
        cache.Mesh()->FaceCount() == 10U,
        "SetVoxel revision must rebuild the mesh.");

    Require(document.RemoveVoxel({0, 0, 0}).Changed,
        "RemoveVoxel fixture mutation failed.");
    synchronized = cache.Synchronize(document, 10U);
    Require(synchronized.Rebuilt() && cache.BuildCount() == 3U &&
        cache.DocumentRevision() == 2U && cache.Mesh() &&
        cache.Mesh()->FaceCount() == 6U,
        "RemoveVoxel revision must rebuild the mesh.");

    synchronized = cache.Synchronize(document, 10U);
    Require(synchronized.Status == VoxelDocumentMeshSyncStatus::Unchanged &&
        cache.BuildCount() == 3U,
        "Stable post-removal revision must not rebuild.");
}

void TestDocumentReplacementAndRelease()
{
    VoxelDocument first = Document({
        Model({1U, 1U, 1U}, {{0U, 0U, 0U, 1U}})});
    VoxelDocument second = Document({
        Model({2U, 1U, 1U}, {
            {0U, 0U, 0U, 2U}, {1U, 0U, 0U, 3U}})});
    VoxelDocumentMeshCache cache;
    Require(cache.Synchronize(first, 20U).Rebuilt(),
        "First active document did not build.");
    Require(cache.Synchronize(second, 21U).Rebuilt() &&
        cache.BuildCount() == 2U && cache.DocumentIdentity() == 21U &&
        cache.DocumentRevision() == 0U && cache.Mesh() &&
        cache.Mesh()->FaceCount() == 10U,
        "Changing the active document must replace the cached mesh.");

    cache.Clear();
    Require(!cache.HasMesh() && cache.Mesh() == nullptr &&
        !cache.DocumentIdentity() && !cache.DocumentRevision() &&
        cache.ModelIndex() == 0U,
        "Project/document close must release all cached mesh state.");
    Require(cache.BuildCount() == 2U,
        "Clearing ownership must preserve diagnostic build history only.");
}
}

// VF-0262 lot 262-1: canonical multiset of faces, independent of emission
// order. Each face key = its 4 corner positions (sorted), normal and colour.
std::vector<std::string> FaceKeys(const VoxelForge::Mesh::MeshData& mesh)
{
    std::vector<std::string> keys;
    keys.reserve(mesh.FaceCount());
    const auto& vertices = mesh.Vertices();
    for (std::size_t face = 0U; face < mesh.FaceCount(); ++face)
    {
        std::array<std::string, 4U> corners;
        for (std::size_t corner = 0U; corner < 4U; ++corner)
        {
            const auto& vertex = vertices[face * 4U + corner];
            corners[corner] =
                std::to_string(vertex.Position[0]) + ',' +
                std::to_string(vertex.Position[1]) + ',' +
                std::to_string(vertex.Position[2]);
        }
        std::sort(corners.begin(), corners.end());
        const auto& normal = vertices[face * 4U].Normal;
        keys.push_back(corners[0] + '|' + corners[1] + '|' + corners[2] +
            '|' + corners[3] + '|' +
            std::to_string(normal[0]) + ',' + std::to_string(normal[1]) +
            ',' + std::to_string(normal[2]) + '|' +
            std::to_string(vertices[face * 4U].ColorIndex));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void TestRegionBuildsMatchFullMesh()
{
    // A solid 2x2x2 block crossing region boundaries plus scattered voxels,
    // inside an 8x8x8 model split into eight 4x4x4 regions.
    std::vector<VoxVoxel> voxels;
    for (std::uint8_t z = 3U; z <= 4U; ++z)
        for (std::uint8_t y = 3U; y <= 4U; ++y)
            for (std::uint8_t x = 3U; x <= 4U; ++x)
                voxels.push_back({x, y, z, 7U});
    voxels.push_back({0U, 0U, 0U, 3U});
    voxels.push_back({7U, 7U, 7U, 4U});
    voxels.push_back({0U, 7U, 3U, 5U});
    const VoxelDocument document =
        Document({Model({8U, 8U, 8U}, voxels)});

    const auto full = VoxelMeshBuilder::Build(document);
    Require(full.Succeeded && full.Mesh, "Full mesh build failed.");
    // The 2x2x2 block only exposes its outer shell: 24 faces, not 48.
    Require(full.Mesh->FaceCount() == 24U + 3U * 6U,
        "Unexpected full mesh face count for the fixture.");

    std::vector<std::string> assembled;
    for (std::int32_t z = 0; z < 8; z += 4)
        for (std::int32_t y = 0; y < 8; y += 4)
            for (std::int32_t x = 0; x < 8; x += 4)
            {
                const auto region = VoxelMeshBuilder::Build(
                    document, {x, y, z}, {x + 3, y + 3, z + 3});
                Require(region.Succeeded && region.Mesh,
                    "Region mesh build failed.");
                const auto keys = FaceKeys(*region.Mesh);
                assembled.insert(
                    assembled.end(), keys.begin(), keys.end());
            }
    std::sort(assembled.begin(), assembled.end());
    Require(assembled == FaceKeys(*full.Mesh),
        "Assembled region meshes must equal the full document mesh.");

    const auto empty = VoxelMeshBuilder::Build(document, {1, 1, 1}, {2, 2, 2});
    Require(empty.Succeeded && empty.Mesh && empty.Mesh->Empty(),
        "An unoccupied region must build an empty mesh.");

    const auto inverted =
        VoxelMeshBuilder::Build(document, {4, 4, 4}, {3, 3, 3});
    Require(!inverted.Succeeded &&
            inverted.Error == MeshBuildError::InvalidSource,
        "Inverted region bounds must be rejected.");
}

// VF-0262 lot 262-3: the incremental chunked cache must stay equivalent to a
// fresh full build after every kind of edit, and only fall back to the full
// rebuild when the revision journal cannot answer.
void TestIncrementalSynchronizeMatchesFullBuild()
{
    using VoxelForge::Asset::Voxel::VoxelDocumentChange;

    // 40x40x40 model: a solid 4x4x4 block crossing the 32-chunk boundary on
    // every axis, plus far corners, so all eight chunks are populated.
    std::vector<VoxVoxel> voxels;
    for (std::uint8_t z = 30U; z <= 33U; ++z)
        for (std::uint8_t y = 30U; y <= 33U; ++y)
            for (std::uint8_t x = 30U; x <= 33U; ++x)
                voxels.push_back({x, y, z, 9U});
    voxels.push_back({0U, 0U, 0U, 3U});
    voxels.push_back({39U, 39U, 39U, 4U});
    VoxelDocument document = Document({Model({40U, 40U, 40U}, voxels)});

    VoxelDocumentMeshCache cache;
    Require(cache.Synchronize(document, 42U).Rebuilt() &&
        cache.FullRebuildCount() == 1U &&
        cache.IncrementalRebuildCount() == 0U &&
        cache.ChunkCount() == 8U,
        "Initial synchronization must fully build all eight chunks.");
    Require(cache.LastSyncWasFullRebuild() &&
        cache.LastSyncTouchedChunks().size() == 8U &&
        cache.Chunks().size() == 8U,
        "A full rebuild must report all chunks as touched.");

    const auto verify = [&](const std::string_view message)
    {
        const auto reference = VoxelMeshBuilder::Build(document);
        Require(reference.Succeeded && reference.Mesh,
            "Reference full build failed.");
        Require(cache.Mesh() &&
            FaceKeys(*cache.Mesh()) == FaceKeys(*reference.Mesh), message);
    };
    verify("Initial assembled mesh must match the full build.");

    Require(document.SetVoxel({10, 10, 10}, 5U).Changed,
        "Interior fixture mutation failed.");
    Require(cache.Synchronize(document, 42U).Rebuilt() &&
        cache.IncrementalRebuildCount() == 1U &&
        cache.LastRebuildChunkCount() == 1U,
        "An interior edit must rebuild exactly one chunk.");
    Require(!cache.LastSyncWasFullRebuild() &&
        cache.LastSyncTouchedChunks() ==
            std::vector<VoxelDocumentMeshCache::ChunkKey>{{0, 0, 0}},
        "An interior edit must report exactly its own chunk as touched.");
    verify("Mesh after an interior edit must match the full build.");
    Require(cache.Synchronize(document, 42U).Succeeded &&
        cache.LastSyncTouchedChunks().empty(),
        "An unchanged synchronization must report no touched chunks.");

    Require(document.SetVoxel({31, 10, 10}, 6U).Changed,
        "Border fixture mutation failed.");
    Require(cache.Synchronize(document, 42U).Rebuilt() &&
        cache.IncrementalRebuildCount() == 2U &&
        cache.LastRebuildChunkCount() == 2U,
        "A border edit must rebuild the chunk and its axis neighbour.");
    verify("Mesh after a border edit must match the full build.");

    // VF-0262 (262-3bis): a pure recolor on the same border position cannot
    // flip neighbour face visibility -> exactly one chunk.
    Require(document.SetVoxel({31, 10, 10}, 7U).Changed,
        "Border recolor fixture mutation failed.");
    Require(cache.Synchronize(document, 42U).Rebuilt() &&
        cache.LastRebuildChunkCount() == 1U,
        "A border recolor must rebuild only the touched chunk.");
    verify("Mesh after a border recolor must match the full build.");

    Require(document.RemoveVoxel({32, 30, 31}).Changed,
        "Boundary removal fixture mutation failed.");
    Require(cache.Synchronize(document, 42U).Rebuilt(),
        "Boundary removal synchronization failed.");
    verify("Mesh after a boundary removal must match the full build.");

    const std::vector<VoxelDocumentChange> batch{
        {0U, {2, 2, 2}, false, 0U, true, 8U},
        {0U, {33, 2, 2}, false, 0U, true, 8U},
        {0U, {30, 30, 30}, true, 9U, true, 10U}};
    Require(document.ApplyVoxelChanges(batch).Changed,
        "Composite fixture batch failed.");
    Require(cache.Synchronize(document, 42U).Rebuilt(),
        "Composite batch synchronization failed.");
    verify("Mesh after a composite batch must match the full build.");

    // Palette-only revision: geometry untouched, no rebuild at all.
    const std::size_t buildsBefore = cache.BuildCount();
    Require(document.SetPaletteColor(9U, {1U, 2U, 3U, 4U}).Changed,
        "Palette fixture mutation failed.");
    const auto paletteSync = cache.Synchronize(document, 42U);
    Require(paletteSync.Succeeded &&
        paletteSync.Status == VoxelDocumentMeshSyncStatus::Unchanged &&
        cache.BuildCount() == buildsBefore &&
        cache.DocumentRevision() == document.GetRevision(),
        "Palette-only revisions must adopt the revision without a rebuild.");
    Require(!cache.LastSyncWasFullRebuild() &&
        cache.LastSyncTouchedChunks().empty(),
        "Palette-only revisions must report no touched chunks.");

    // Journal eviction: churn more revisions than the ring keeps.
    Require(document.SetVoxel({5, 5, 5}, 7U).Changed,
        "Churn seed mutation failed.");
    for (std::size_t index = 0U;
         index <= VoxelDocument::MaximumJournaledRevisions; ++index)
    {
        Require(document.ReplaceVoxelColor(
            {5, 5, 5}, (index % 2U == 0U) ? 8U : 7U).Changed,
            "Churn mutation failed.");
    }
    const std::size_t fullRebuilds = cache.FullRebuildCount();
    Require(cache.Synchronize(document, 42U).Rebuilt() &&
        cache.FullRebuildCount() == fullRebuilds + 1U,
        "An evicted journal must fall back to the full chunked rebuild.");
    verify("Mesh after the fallback rebuild must match the full build.");

    // Identity change bypasses the incremental path entirely.
    Require(cache.Synchronize(document, 43U).Rebuilt() &&
        cache.FullRebuildCount() == fullRebuilds + 2U,
        "An identity change must fully rebuild.");

    cache.Clear();
    Require(!cache.HasMesh() && cache.ChunkCount() == 0U,
        "Clear must release the chunk meshes.");
}

int main()
{
    try
    {
        TestEmptyAndSingleVoxelMesh();
        TestMultiModelAndInvalidIndex();
        TestRevisionSynchronization();
        TestDocumentReplacementAndRelease();
        TestRegionBuildsMatchFullMesh();
        TestIncrementalSynchronizeMatchesFullBuild();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
