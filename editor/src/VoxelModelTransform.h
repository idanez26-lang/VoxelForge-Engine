#pragma once

#include "EditorMath.h"
#include "VoxelSelection/VoxelRay.h"

#include "VoxelForge/Mesh/MeshData.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace VoxelForge::Editor
{

[[nodiscard]] inline Vec3 VoxelGridToViewport(
    const Vec3 gridPosition,
    const Vec3 modelCenter) noexcept
{
    return gridPosition - modelCenter;
}

[[nodiscard]] inline VoxelRay ViewportToVoxelGrid(
    const VoxelRay& viewportRay,
    const Vec3 modelCenter) noexcept
{
    return {viewportRay.Origin + modelCenter, viewportRay.Direction};
}

[[nodiscard]] inline Vec3 CalculateVoxelMeshCenter(
    const Mesh::MeshData& mesh) noexcept
{
    if (mesh.Empty()) return {};
    std::array<float, 3> minimum = mesh.Vertices().front().Position;
    std::array<float, 3> maximum = minimum;
    for (const Mesh::MeshVertex& vertex : mesh.Vertices())
    {
        for (std::size_t axis = 0U; axis < 3U; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], vertex.Position[axis]);
            maximum[axis] = std::max(maximum[axis], vertex.Position[axis]);
        }
    }
    return {
        (minimum[0] + maximum[0]) * 0.5F,
        (minimum[1] + maximum[1]) * 0.5F,
        (minimum[2] + maximum[2]) * 0.5F};
}

} // namespace VoxelForge::Editor
