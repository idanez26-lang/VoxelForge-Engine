#pragma once

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>

namespace VoxelForge::Editor
{

enum class ViewportBackground : std::uint8_t
{
    Dark,
    Neutral,
    Light
};

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
    [[nodiscard]] bool UpdateStatistics(
        const Voxel::VoxelModel& model,
        const Mesh::MeshData& mesh) noexcept;
    void Clear() noexcept;

    [[nodiscard]] bool HasModel() const noexcept;
    [[nodiscard]] const std::string& Name() const noexcept;
    [[nodiscard]] const VoxelViewportStatistics& Statistics() const noexcept;
    void SetGridVisible(bool visible) noexcept;
    void SetAxesVisible(bool visible) noexcept;
    void SetBackground(ViewportBackground background) noexcept;
    [[nodiscard]] bool IsGridVisible() const noexcept;
    [[nodiscard]] bool AreAxesVisible() const noexcept;
    [[nodiscard]] ViewportBackground Background() const noexcept;
    [[nodiscard]] std::array<float, 4> BackgroundColor() const noexcept;

private:
    std::string name_;
    VoxelViewportStatistics statistics_{};
    bool hasModel_ = false;
    bool gridVisible_ = true;
    bool axesVisible_ = true;
    ViewportBackground background_ = ViewportBackground::Dark;
};

} // namespace VoxelForge::Editor
