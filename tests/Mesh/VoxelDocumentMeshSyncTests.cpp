#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"

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

int main()
{
    try
    {
        TestEmptyAndSingleVoxelMesh();
        TestMultiModelAndInvalidIndex();
        TestRevisionSynchronization();
        TestDocumentReplacementAndRelease();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
