#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

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

} // namespace VoxelForge::Mesh
