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

private:
    friend class VoxelMeshBuilder;

    std::vector<MeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
};

} // namespace VoxelForge::Mesh
