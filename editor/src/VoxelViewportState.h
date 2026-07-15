#pragma once

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>

namespace VoxelForge::Editor
{

struct VoxelViewportStatistics final
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Depth = 0;
    std::size_t OccupiedVoxelCount = 0;
    std::size_t VertexCount = 0;
    std::size_t TriangleCount = 0;
};

[[nodiscard]] std::array<float, 4> ToViewportColor(
    Voxel::VoxelColor color) noexcept;

class VoxelViewportState final
{
public:
    [[nodiscard]] bool Replace(
        std::string name,
        const Voxel::VoxelModel& model,
        const Mesh::MeshData& mesh);
    void Clear() noexcept;

    [[nodiscard]] bool HasModel() const noexcept;
    [[nodiscard]] const std::string& Name() const noexcept;
    [[nodiscard]] const VoxelViewportStatistics& Statistics() const noexcept;

private:
    std::string name_;
    VoxelViewportStatistics statistics_{};
    bool hasModel_ = false;
};

} // namespace VoxelForge::Editor
