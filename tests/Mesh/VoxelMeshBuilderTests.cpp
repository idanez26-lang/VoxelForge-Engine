#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Mesh/MeshVertex.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include "VoxelForge/Voxel/Voxel.h"
#include "VoxelForge/Voxel/VoxelGrid.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using VoxelForge::Mesh::MeshBuildError;
using VoxelForge::Mesh::MeshBuildResult;
using VoxelForge::Mesh::MeshData;
using VoxelForge::Mesh::MeshVertex;
using VoxelForge::Mesh::VoxelMeshBuilder;
using VoxelForge::Voxel::Voxel;
using VoxelForge::Voxel::VoxelGrid;

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

Voxel Occupied(const std::uint8_t colorIndex)
{
    return {colorIndex, Voxel::OccupiedFlag};
}

VoxelGrid Grid(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t depth)
{
    VoxelGrid grid;
    Require(grid.Resize(width, height, depth), "Test grid resize failed.");
    return grid;
}

const MeshData& RequireMesh(
    const MeshBuildResult& result,
    const std::string_view message)
{
    Require(result.Succeeded, message);
    Require(
        result.Error == MeshBuildError::None && result.Mesh.has_value(),
        "Successful build must contain a mesh and no error.");
    return *result.Mesh;
}

void RequireCounts(
    const VoxelGrid& grid,
    const std::size_t expectedFaces)
{
    const MeshBuildResult result = VoxelMeshBuilder::Build(grid);
    const MeshData& mesh = RequireMesh(result, "Mesh generation failed.");
    Require(mesh.FaceCount() == expectedFaces, "Face count is incorrect.");
    Require(
        mesh.VertexCount() == expectedFaces * 4U,
        "Vertex count is incorrect.");
    Require(
        mesh.IndexCount() == expectedFaces * 6U,
        "Index count is incorrect.");
    Require(
        mesh.TriangleCount() == expectedFaces * 2U,
        "Triangle count is incorrect.");
}

void TestEmptyGrids()
{
    static_assert(std::is_same_v<
        decltype(std::declval<MeshData&>().Vertices()),
        const std::vector<MeshVertex>&>);
    static_assert(std::is_same_v<
        decltype(std::declval<MeshData&>().Indices()),
        const std::vector<std::uint32_t>&>);

    VoxelGrid canonical;
    const MeshBuildResult canonicalResult =
        VoxelMeshBuilder::Build(canonical);
    const MeshData& canonicalMesh = RequireMesh(
        canonicalResult,
        "Canonical empty grid build failed.");
    Require(canonicalMesh.Empty(), "Canonical empty grid produced geometry.");

    const VoxelGrid allocated = Grid(3U, 2U, 4U);
    const MeshBuildResult allocatedResult =
        VoxelMeshBuilder::Build(allocated);
    const MeshData& allocatedMesh = RequireMesh(
        allocatedResult,
        "Allocated empty grid build failed.");
    Require(allocatedMesh.Empty(), "Empty allocated grid produced geometry.");
}

void TestSimpleFaceCounts()
{
    VoxelGrid single = Grid(1U, 1U, 1U);
    Require(single.Set(0U, 0U, 0U, Occupied(7U)), "Single Set failed.");
    RequireCounts(single, 6U);

    VoxelGrid adjacent = Grid(2U, 1U, 1U);
    Require(adjacent.Set(0U, 0U, 0U, Occupied(1U)), "Adjacent Set failed.");
    Require(adjacent.Set(1U, 0U, 0U, Occupied(2U)), "Adjacent Set failed.");
    RequireCounts(adjacent, 10U);

    VoxelGrid adjacentY = Grid(1U, 2U, 1U);
    Require(adjacentY.Set(0U, 0U, 0U, Occupied(1U)), "Adjacent Y Set failed.");
    Require(adjacentY.Set(0U, 1U, 0U, Occupied(2U)), "Adjacent Y Set failed.");
    RequireCounts(adjacentY, 10U);

    VoxelGrid adjacentZ = Grid(1U, 1U, 2U);
    Require(adjacentZ.Set(0U, 0U, 0U, Occupied(1U)), "Adjacent Z Set failed.");
    Require(adjacentZ.Set(0U, 0U, 1U, Occupied(2U)), "Adjacent Z Set failed.");
    RequireCounts(adjacentZ, 10U);

    VoxelGrid paletteZeroNeighbor = Grid(2U, 1U, 1U);
    Require(
        paletteZeroNeighbor.Set(0U, 0U, 0U, Occupied(0U)),
        "Palette zero neighbor Set failed.");
    Require(
        paletteZeroNeighbor.Set(1U, 0U, 0U, Occupied(3U)),
        "Palette zero neighbor Set failed.");
    RequireCounts(paletteZeroNeighbor, 10U);

    VoxelGrid separated = Grid(3U, 1U, 1U);
    Require(separated.Set(0U, 0U, 0U, Occupied(1U)), "Separated Set failed.");
    Require(separated.Set(2U, 0U, 0U, Occupied(2U)), "Separated Set failed.");
    RequireCounts(separated, 12U);

    VoxelGrid line = Grid(3U, 1U, 1U);
    line.Fill(Occupied(3U));
    RequireCounts(line, 14U);

    VoxelGrid block = Grid(2U, 2U, 2U);
    block.Fill(Occupied(4U));
    RequireCounts(block, 24U);

    VoxelGrid explicitEmpty = Grid(2U, 1U, 1U);
    Require(
        explicitEmpty.Set(0U, 0U, 0U, {9U, 0U}),
        "Empty colored voxel Set failed.");
    Require(
        explicitEmpty.Set(1U, 0U, 0U, {8U, 1U << 5U}),
        "Reserved-only voxel Set failed.");
    RequireCounts(explicitEmpty, 0U);
}

std::array<float, 3> Subtract(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right)
{
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

std::array<float, 3> Cross(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right)
{
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]};
}

float Dot(
    const std::array<float, 3>& left,
    const std::array<float, 3>& right)
{
    return left[0] * right[0] + left[1] * right[1] +
        left[2] * right[2];
}

void TestSingleVoxelGeometry()
{
    VoxelGrid grid = Grid(1U, 1U, 1U);
    Require(
        grid.Set(0U, 0U, 0U, Occupied(0U)),
        "Occupied palette index 0 Set failed.");
    const MeshBuildResult result = VoxelMeshBuilder::Build(grid);
    const MeshData& mesh = RequireMesh(
        result,
        "Single voxel geometry build failed.");

    RequireCounts(grid, 6U);

    std::array<std::array<float, 3>, 6> expectedNormals = {{
        {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
        {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 1.0F}}};

    for (const MeshVertex& vertex : mesh.Vertices())
    {
        Require(
            vertex.Position[0] >= 0.0F && vertex.Position[0] <= 1.0F &&
                vertex.Position[1] >= 0.0F && vertex.Position[1] <= 1.0F &&
                vertex.Position[2] >= 0.0F && vertex.Position[2] <= 1.0F,
            "Single voxel position lies outside [0,1].");
        Require(vertex.ColorIndex == 0U, "Palette index 0 was not copied.");
        Require(
            vertex.Padding == std::array<std::uint8_t, 3>{},
            "Mesh vertex padding was not initialized.");
    }

    for (const std::array<float, 3>& normal : expectedNormals)
    {
        const std::size_t count = static_cast<std::size_t>(std::count_if(
            mesh.Vertices().begin(),
            mesh.Vertices().end(),
            [&normal](const MeshVertex& vertex)
            {
                return vertex.Normal == normal;
            }));
        Require(count == 4U, "A face normal is missing or duplicated.");
    }

    for (std::size_t face = 0U; face < mesh.FaceCount(); ++face)
    {
        const std::uint32_t base = static_cast<std::uint32_t>(face * 4U);
        const std::array<std::uint32_t, 6> expected = {
            base, base + 1U, base + 2U, base, base + 2U, base + 3U};

        for (std::size_t local = 0U; local < expected.size(); ++local)
        {
            Require(
                mesh.Indices()[face * 6U + local] == expected[local],
                "Face index pattern is inconsistent.");
        }
    }

    for (std::size_t triangle = 0U;
         triangle < mesh.TriangleCount();
         ++triangle)
    {
        const std::uint32_t index0 = mesh.Indices()[triangle * 3U];
        const std::uint32_t index1 = mesh.Indices()[triangle * 3U + 1U];
        const std::uint32_t index2 = mesh.Indices()[triangle * 3U + 2U];
        Require(
            index0 < mesh.VertexCount() && index1 < mesh.VertexCount() &&
                index2 < mesh.VertexCount(),
            "Triangle index is outside the vertex array.");

        const MeshVertex& vertex0 = mesh.Vertices()[index0];
        const MeshVertex& vertex1 = mesh.Vertices()[index1];
        const MeshVertex& vertex2 = mesh.Vertices()[index2];
        const std::array<float, 3> cross = Cross(
            Subtract(vertex1.Position, vertex0.Position),
            Subtract(vertex2.Position, vertex0.Position));
        Require(Dot(cross, cross) > 0.0F, "Triangle is degenerate.");
        Require(
            Dot(cross, vertex0.Normal) > 0.0F,
            "Triangle winding does not match its outward normal.");
    }
}

void TestBordersAndCorners()
{
    for (std::uint32_t z = 0U; z < 2U; ++z)
    {
        for (std::uint32_t y = 0U; y < 2U; ++y)
        {
            for (std::uint32_t x = 0U; x < 2U; ++x)
            {
                VoxelGrid grid = Grid(2U, 2U, 2U);
                Require(grid.Set(x, y, z, Occupied(9U)), "Corner Set failed.");
                RequireCounts(grid, 6U);
            }
        }
    }

    VoxelGrid shifted = Grid(3U, 4U, 5U);
    Require(shifted.Set(2U, 3U, 4U, Occupied(5U)), "Shifted Set failed.");
    const MeshBuildResult shiftedResult = VoxelMeshBuilder::Build(shifted);
    const MeshData& mesh = RequireMesh(
        shiftedResult,
        "Shifted corner build failed.");

    for (const MeshVertex& vertex : mesh.Vertices())
    {
        Require(
            (vertex.Position[0] == 2.0F || vertex.Position[0] == 3.0F) &&
                (vertex.Position[1] == 3.0F || vertex.Position[1] == 4.0F) &&
                (vertex.Position[2] == 4.0F || vertex.Position[2] == 5.0F),
            "Shifted voxel bounds are incorrect.");
        Require(vertex.ColorIndex == 5U, "Palette index was not copied.");
    }

    const std::array<float, 3> lower = {2.0F, 3.0F, 4.0F};
    const std::array<float, 3> upper = {3.0F, 4.0F, 5.0F};

    for (std::size_t axis = 0U; axis < 3U; ++axis)
    {
        const bool hasLower = std::ranges::any_of(
            mesh.Vertices(),
            [axis, &lower](const MeshVertex& vertex)
            {
                return vertex.Position[axis] == lower[axis];
            });
        const bool hasUpper = std::ranges::any_of(
            mesh.Vertices(),
            [axis, &upper](const MeshVertex& vertex)
            {
                return vertex.Position[axis] == upper[axis];
            });
        Require(
            hasLower && hasUpper,
            "Shifted voxel does not cover both exact cell boundaries.");
    }
}

void TestSourceIntegrityClearAndDeterminism()
{
    VoxelGrid grid = Grid(3U, 2U, 2U);
    Require(grid.Set(0U, 0U, 0U, Occupied(4U)), "Integrity Set failed.");
    Require(grid.Set(2U, 1U, 1U, Occupied(8U)), "Integrity Set failed.");
    const std::vector<Voxel> sourceData = grid.Data();
    const std::size_t occupiedCount = grid.OccupiedVoxelCount();

    MeshBuildResult first = VoxelMeshBuilder::Build(grid);
    MeshBuildResult second = VoxelMeshBuilder::Build(grid);
    const MeshData& firstMesh = RequireMesh(first, "First deterministic build failed.");
    const MeshData& secondMesh = RequireMesh(second, "Second deterministic build failed.");
    Require(
        firstMesh.Vertices() == secondMesh.Vertices() &&
            firstMesh.Indices() == secondMesh.Indices(),
        "Repeated builds are not deterministic.");
    Require(
        grid.Data() == sourceData &&
            grid.OccupiedVoxelCount() == occupiedCount,
        "Mesh generation modified the source grid.");

    MeshData cleared = std::move(*first.Mesh);
    cleared.Clear();
    Require(
        cleared.Empty() && cleared.VertexCount() == 0U &&
            cleared.IndexCount() == 0U && cleared.FaceCount() == 0U &&
            cleared.TriangleCount() == 0U,
        "MeshData::Clear did not restore an empty mesh.");
}

void TestFaceLimit()
{
    VoxelGrid grid = Grid(48U, 48U, 48U);

    for (std::uint32_t z = 0U; z < grid.Depth(); ++z)
    {
        for (std::uint32_t y = 0U; y < grid.Height(); ++y)
        {
            for (std::uint32_t x = 0U; x < grid.Width(); ++x)
            {
                if ((x + y + z) % 2U == 0U)
                {
                    Require(
                        grid.Set(x, y, z, Occupied(1U)),
                        "Limit grid Set failed.");
                }
            }
        }
    }

    const MeshBuildResult result = VoxelMeshBuilder::Build(grid);
    Require(!result.Succeeded, "Excessive visible mesh was accepted.");
    Require(
        result.Error == MeshBuildError::TooLarge && !result.Mesh,
        "Size failure must return no partial mesh.");
}

void PrintTypeSizes()
{
    std::cout << "sizeof(MeshVertex)=" << sizeof(MeshVertex) << '\n'
              << "alignof(MeshVertex)=" << alignof(MeshVertex) << '\n'
              << "sizeof(MeshData)=" << sizeof(MeshData) << '\n';
}
}

int main()
{
    try
    {
        TestEmptyGrids();
        TestSimpleFaceCounts();
        TestSingleVoxelGeometry();
        TestBordersAndCorners();
        TestSourceIntegrityClearAndDeterminism();
        TestFaceLimit();
        PrintTypeSizes();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
