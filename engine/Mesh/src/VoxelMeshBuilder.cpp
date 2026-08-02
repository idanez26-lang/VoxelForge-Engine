#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace VoxelForge::Mesh
{

namespace
{
struct FaceDefinition final
{
    std::int32_t DeltaX = 0;
    std::int32_t DeltaY = 0;
    std::int32_t DeltaZ = 0;
    std::array<float, 3> Normal{};
    std::array<std::array<float, 3>, 4> Corners{};
};

// Vertices are counter-clockwise when viewed from outside the voxel. The
// triangles (0,1,2) and (0,2,3) therefore produce the declared outward normal
// in a right-handed X/Y/Z coordinate system.
constexpr std::array<FaceDefinition, 6> Faces = {{
    {-1, 0, 0, {-1.0F, 0.0F, 0.0F},
        {{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
          {0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 0.0F}}}},
    {1, 0, 0, {1.0F, 0.0F, 0.0F},
        {{{1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F},
          {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}}},
    {0, -1, 0, {0.0F, -1.0F, 0.0F},
        {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
          {1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}},
    {0, 1, 0, {0.0F, 1.0F, 0.0F},
        {{{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 1.0F},
          {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}}}},
    {0, 0, -1, {0.0F, 0.0F, -1.0F},
        {{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
          {1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}}},
    {0, 0, 1, {0.0F, 0.0F, 1.0F},
        {{{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 1.0F},
          {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 1.0F}}}}
}};

MeshBuildResult Failure(const MeshBuildError error, std::string message)
{
    return {false, error, std::move(message), std::nullopt};
}

bool IsFaceVisible(
    const Voxel::VoxelGrid& grid,
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const FaceDefinition& face) noexcept
{
    if ((face.DeltaX < 0 && x == 0U) ||
        (face.DeltaY < 0 && y == 0U) ||
        (face.DeltaZ < 0 && z == 0U) ||
        (face.DeltaX > 0 && x == grid.Width() - 1U) ||
        (face.DeltaY > 0 && y == grid.Height() - 1U) ||
        (face.DeltaZ > 0 && z == grid.Depth() - 1U))
    {
        return true;
    }

    const std::uint32_t neighborX = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(x) + face.DeltaX);
    const std::uint32_t neighborY = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(y) + face.DeltaY);
    const std::uint32_t neighborZ = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(z) + face.DeltaZ);
    const Voxel::Voxel* neighbor = grid.Get(neighborX, neighborY, neighborZ);
    return neighbor == nullptr || !neighbor->IsOccupied();
}

bool CountVisibleFaces(
    const Voxel::VoxelGrid& grid,
    std::size_t& visibleFaceCount) noexcept
{
    visibleFaceCount = 0U;

    for (std::uint32_t z = 0U; z < grid.Depth(); ++z)
    {
        for (std::uint32_t y = 0U; y < grid.Height(); ++y)
        {
            for (std::uint32_t x = 0U; x < grid.Width(); ++x)
            {
                const Voxel::Voxel* voxel = grid.Get(x, y, z);

                if (voxel == nullptr || !voxel->IsOccupied())
                {
                    continue;
                }

                for (const FaceDefinition& face : Faces)
                {
                    if (IsFaceVisible(grid, x, y, z, face))
                    {
                        ++visibleFaceCount;

                        if (visibleFaceCount >
                            VoxelMeshBuilder::MaximumFaceCount)
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }

    return true;
}

}

MeshBuildResult VoxelMeshBuilder::Build(const Voxel::VoxelGrid& grid)
{
    std::size_t faceCount = 0U;

    if (!CountVisibleFaces(grid, faceCount))
    {
        return Failure(
            MeshBuildError::TooLarge,
            "Visible voxel mesh exceeds the v1 face limit.");
    }

    constexpr std::size_t maximumIndex =
        std::numeric_limits<std::uint32_t>::max();

    if (faceCount > maximumIndex / 4U || faceCount > maximumIndex / 6U)
    {
        return Failure(
            MeshBuildError::TooLarge,
            "Voxel mesh exceeds 32-bit vertex or index limits.");
    }

    const std::size_t vertexCount = faceCount * 4U;
    const std::size_t indexCount = faceCount * 6U;

    if (vertexCount > MaximumVertexCount || indexCount > MaximumIndexCount)
    {
        return Failure(
            MeshBuildError::TooLarge,
            "Voxel mesh exceeds the v1 CPU allocation limit.");
    }

    MeshData mesh;

    try
    {
        mesh.vertices_.reserve(vertexCount);
        mesh.indices_.reserve(indexCount);

        const auto appendFace = [&mesh](
            const FaceDefinition& face,
            const std::uint32_t x,
            const std::uint32_t y,
            const std::uint32_t z,
            const std::uint8_t colorIndex)
        {
            const std::uint32_t firstVertex =
                static_cast<std::uint32_t>(mesh.vertices_.size());

            for (const std::array<float, 3>& corner : face.Corners)
            {
                MeshVertex vertex;
                vertex.Position = {
                    static_cast<float>(x) + corner[0],
                    static_cast<float>(y) + corner[1],
                    static_cast<float>(z) + corner[2]};
                vertex.Normal = face.Normal;
                vertex.ColorIndex = colorIndex;
                mesh.vertices_.push_back(vertex);
            }

            constexpr std::array<std::uint32_t, 6> localIndices = {
                0U, 1U, 2U, 0U, 2U, 3U};

            for (const std::uint32_t localIndex : localIndices)
            {
                mesh.indices_.push_back(firstVertex + localIndex);
            }
        };

        for (std::uint32_t z = 0U; z < grid.Depth(); ++z)
        {
            for (std::uint32_t y = 0U; y < grid.Height(); ++y)
            {
                for (std::uint32_t x = 0U; x < grid.Width(); ++x)
                {
                    const Voxel::Voxel* voxel = grid.Get(x, y, z);

                    if (voxel == nullptr || !voxel->IsOccupied())
                    {
                        continue;
                    }

                    for (const FaceDefinition& face : Faces)
                    {
                        if (IsFaceVisible(grid, x, y, z, face))
                        {
                            appendFace(face, x, y, z, voxel->ColorIndex);
                        }
                    }
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return Failure(
            MeshBuildError::AllocationFailure,
            "Unable to allocate CPU memory for the voxel mesh.");
    }
    catch (const std::length_error&)
    {
        return Failure(
            MeshBuildError::AllocationFailure,
            "Voxel mesh allocation exceeds container limits.");
    }

    if (mesh.VertexCount() != vertexCount || mesh.IndexCount() != indexCount)
    {
        return Failure(
            MeshBuildError::InconsistentResult,
            "Voxel mesh generation produced inconsistent buffer sizes.");
    }

    return {
        true,
        MeshBuildError::None,
        "Voxel mesh generation succeeded.",
        std::move(mesh)};
}

// Shared document mesh construction (VF-0262 lot 262-1). A null region
// builds the whole sub-model; otherwise only voxels inside the inclusive
// region emit faces. Visibility always consults the whole sub-model, so
// meshes built over a partition of space assemble seamlessly.
MeshBuildResult VoxelMeshBuilder::BuildDocumentMesh(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t modelIndex,
    const Asset::Voxel::VoxelPosition* const regionMinimum,
    const Asset::Voxel::VoxelPosition* const regionMaximum)
{
    if (document.GetModelCount() == 0U && modelIndex == 0U)
    {
        return {
            true,
            MeshBuildError::None,
            "Empty voxel document mesh generation succeeded.",
            MeshData{}};
    }

    const Asset::Voxel::VoxelSubModel* model = document.GetModel(modelIndex);
    if (model == nullptr)
    {
        return Failure(
            MeshBuildError::InvalidSource,
            "Voxel document sub-model index is invalid.");
    }

    struct DocumentVoxel final
    {
        Asset::Voxel::VoxelPosition Position;
        Asset::Voxel::Voxel Value;
    };
    std::vector<DocumentVoxel> voxels;
    try
    {
        voxels.reserve(model->VoxelCount());
        model->ForEachVoxel(
            [&voxels, regionMinimum, regionMaximum](
                const Asset::Voxel::VoxelPosition& position,
                const Asset::Voxel::Voxel& voxel)
            {
                if (regionMinimum != nullptr &&
                    (position.X < regionMinimum->X ||
                     position.Y < regionMinimum->Y ||
                     position.Z < regionMinimum->Z ||
                     position.X > regionMaximum->X ||
                     position.Y > regionMaximum->Y ||
                     position.Z > regionMaximum->Z))
                {
                    return;
                }
                voxels.push_back({position, voxel});
            });
        std::sort(
            voxels.begin(), voxels.end(),
            [](const DocumentVoxel& left, const DocumentVoxel& right)
            {
                if (left.Position.Z != right.Position.Z)
                    return left.Position.Z < right.Position.Z;
                if (left.Position.Y != right.Position.Y)
                    return left.Position.Y < right.Position.Y;
                return left.Position.X < right.Position.X;
            });
    }
    catch (const std::bad_alloc&)
    {
        return Failure(
            MeshBuildError::AllocationFailure,
            "Unable to allocate the voxel document traversal buffer.");
    }
    catch (const std::length_error&)
    {
        return Failure(
            MeshBuildError::AllocationFailure,
            "Voxel document traversal exceeds container limits.");
    }

    const auto faceVisible = [model](
        const Asset::Voxel::VoxelPosition& position,
        const FaceDefinition& face)
    {
        return !model->HasVoxel({
            position.X + face.DeltaX,
            position.Y + face.DeltaY,
            position.Z + face.DeltaZ});
    };

    std::size_t faceCount = 0U;
    for (const DocumentVoxel& voxel : voxels)
    {
        for (const FaceDefinition& face : Faces)
        {
            if (faceVisible(voxel.Position, face) &&
                ++faceCount > VoxelMeshBuilder::MaximumFaceCount)
            {
                return Failure(
                    MeshBuildError::TooLarge,
                    "Visible voxel document mesh exceeds the v1 face limit.");
            }
        }
    }

    constexpr std::size_t maximumIndex =
        std::numeric_limits<std::uint32_t>::max();
    if (faceCount > maximumIndex / 4U || faceCount > maximumIndex / 6U ||
        faceCount * 4U > VoxelMeshBuilder::MaximumVertexCount ||
        faceCount * 6U > VoxelMeshBuilder::MaximumIndexCount)
    {
        return Failure(
            MeshBuildError::TooLarge,
            "Voxel document mesh exceeds the v1 CPU allocation limit.");
    }

    MeshData mesh;
    try
    {
        mesh.vertices_.reserve(faceCount * 4U);
        mesh.indices_.reserve(faceCount * 6U);
        for (const DocumentVoxel& voxel : voxels)
        {
            for (const FaceDefinition& face : Faces)
            {
                if (!faceVisible(voxel.Position, face)) continue;
                const std::uint32_t firstVertex =
                    static_cast<std::uint32_t>(mesh.vertices_.size());
                for (const std::array<float, 3>& corner : face.Corners)
                {
                    MeshVertex vertex;
                    vertex.Position = {
                        static_cast<float>(voxel.Position.X) + corner[0],
                        static_cast<float>(voxel.Position.Y) + corner[1],
                        static_cast<float>(voxel.Position.Z) + corner[2]};
                    vertex.Normal = face.Normal;
                    vertex.ColorIndex = voxel.Value.PaletteIndex;
                    mesh.vertices_.push_back(vertex);
                }
                constexpr std::array<std::uint32_t, 6> localIndices = {
                    0U, 1U, 2U, 0U, 2U, 3U};
                for (const std::uint32_t localIndex : localIndices)
                    mesh.indices_.push_back(firstVertex + localIndex);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return Failure(
            MeshBuildError::AllocationFailure,
            "Unable to allocate CPU memory for the voxel document mesh.");
    }
    catch (const std::length_error&)
    {
        return Failure(
            MeshBuildError::AllocationFailure,
            "Voxel document mesh allocation exceeds container limits.");
    }

    if (mesh.FaceCount() != faceCount)
    {
        return Failure(
            MeshBuildError::InconsistentResult,
            "Voxel document mesh generation produced inconsistent buffers.");
    }
    return {
        true,
        MeshBuildError::None,
        "Voxel document mesh generation succeeded.",
        std::move(mesh)};
}

MeshBuildResult VoxelMeshBuilder::Build(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t modelIndex)
{
    return BuildDocumentMesh(document, modelIndex, nullptr, nullptr);
}

MeshBuildResult VoxelMeshBuilder::Build(
    const Asset::Voxel::VoxelDocument& document,
    const Asset::Voxel::VoxelPosition& regionMinimum,
    const Asset::Voxel::VoxelPosition& regionMaximum,
    const std::size_t modelIndex)
{
    if (regionMinimum.X > regionMaximum.X ||
        regionMinimum.Y > regionMaximum.Y ||
        regionMinimum.Z > regionMaximum.Z)
    {
        return Failure(
            MeshBuildError::InvalidSource,
            "Voxel mesh region bounds are inverted.");
    }
    return BuildDocumentMesh(
        document, modelIndex, &regionMinimum, &regionMaximum);
}

} // namespace VoxelForge::Mesh
