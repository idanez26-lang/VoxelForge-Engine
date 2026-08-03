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

private:
    friend class VoxelMeshBuilder;

    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace VoxelForge::Mesh
