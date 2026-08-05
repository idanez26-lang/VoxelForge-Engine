#pragma once

#include "MeshVertex.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VoxelForge::Mesh
{

class VoxelMeshBuilder;

class MeshData final
{
public:
    [[nodiscard]] const std::vector<MeshVertex>& Vertices() const noexcept
    {
        return vertices_;
    }

    [[nodiscard]] const std::vector<std::uint32_t>& Indices() const noexcept
    {
        return indices_;
    }

    [[nodiscard]] std::size_t VertexCount() const noexcept
    {
        return vertices_.size();
    }

    [[nodiscard]] std::size_t IndexCount() const noexcept
    {
        return indices_.size();
    }

    [[nodiscard]] std::size_t TriangleCount() const noexcept
    {
        return indices_.size() / 3U;
    }

    [[nodiscard]] std::size_t FaceCount() const noexcept
    {
        return indices_.size() / 6U;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return vertices_.empty() && indices_.empty();
    }

    void Clear() noexcept
    {
        vertices_.clear();
        indices_.clear();
    }

    // VF-0262 (lot 262-3): pre-allocates the buffers before assembly.
    void Reserve(const std::size_t vertexCount, const std::size_t indexCount)
    {
        vertices_.reserve(vertexCount);
        indices_.reserve(indexCount);
    }

    // VF-0262 (lot 262-3): appends another mesh, offsetting its indices by
    // the current vertex count. Used to assemble per-chunk meshes into the
    // single mesh consumed by the renderer (until the partial upload of
    // lot 262-4).
    void Append(const MeshData& other)
    {
        const std::uint32_t vertexOffset =
            static_cast<std::uint32_t>(vertices_.size());
        vertices_.insert(
            vertices_.end(), other.vertices_.begin(), other.vertices_.end());
        indices_.reserve(indices_.size() + other.indices_.size());
        for (const std::uint32_t index : other.indices_)
            indices_.push_back(index + vertexOffset);
    }

    // VF-0265 (lot 3): keeps only the faces whose voxel lies OUTSIDE the
    // inclusive box, and returns them as a new mesh.
    //
    // This is what lets a preview reuse a chunk of the document mesh that
    // merely intersects the dirty region: drop the faces of the dirty voxels,
    // then union with a regional build of the final state. Rebuilding the whole
    // chunk instead would cost 28 ms on a dense document (VF-0264) — nearly two
    // frames for a single chunk.
    //
    // Face layout is the builder's: four vertices then six indices, corners at
    // integer offsets from the voxel, one shared normal. The voxel is therefore
    // recovered exactly as `minimum corner - max(normal, 0)`; coordinates are
    // small integers, exactly representable in float. VoxelChangeOverlayTests
    // and MeshDataFaceFilterTests break immediately if that layout ever moves.
    [[nodiscard]] MeshData FacesOutsideBox(
        const std::int32_t minimumX,
        const std::int32_t minimumY,
        const std::int32_t minimumZ,
        const std::int32_t maximumX,
        const std::int32_t maximumY,
        const std::int32_t maximumZ) const
    {
        MeshData kept;
        const std::size_t faceCount = FaceCount();
        kept.vertices_.reserve(vertices_.size());
        kept.indices_.reserve(indices_.size());
        for (std::size_t face = 0U; face < faceCount; ++face)
        {
            const MeshVertex& first = vertices_[face * 4U];
            float lowest[3] = {
                first.Position[0], first.Position[1], first.Position[2]};
            for (std::size_t corner = 1U; corner < 4U; ++corner)
            {
                const MeshVertex& vertex = vertices_[face * 4U + corner];
                for (std::size_t axis = 0U; axis < 3U; ++axis)
                    lowest[axis] = vertex.Position[axis] < lowest[axis]
                        ? vertex.Position[axis]
                        : lowest[axis];
            }
            const auto voxelAxis = [&](const std::size_t axis) noexcept
            {
                const float normal = first.Normal[axis];
                const float offset = normal > 0.0F ? 1.0F : 0.0F;
                const float value = lowest[axis] - offset;
                return static_cast<std::int32_t>(
                    value < 0.0F ? value - 0.5F : value + 0.5F);
            };
            const std::int32_t x = voxelAxis(0U);
            const std::int32_t y = voxelAxis(1U);
            const std::int32_t z = voxelAxis(2U);
            const bool inside = x >= minimumX && x <= maximumX &&
                y >= minimumY && y <= maximumY && z >= minimumZ &&
                z <= maximumZ;
            if (inside) continue;

            const std::uint32_t firstVertex =
                static_cast<std::uint32_t>(kept.vertices_.size());
            for (std::size_t corner = 0U; corner < 4U; ++corner)
                kept.vertices_.push_back(vertices_[face * 4U + corner]);
            for (const std::uint32_t local : {0U, 1U, 2U, 0U, 2U, 3U})
                kept.indices_.push_back(firstVertex + local);
        }
        return kept;
    }

private:
    friend class VoxelMeshBuilder;

    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace VoxelForge::Mesh
