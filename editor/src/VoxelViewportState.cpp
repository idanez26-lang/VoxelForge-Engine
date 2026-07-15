#include "VoxelViewportState.h"

#include <utility>

namespace VoxelForge::Editor
{

std::array<float, 4> ToViewportColor(const Voxel::VoxelColor color) noexcept
{
    constexpr float scale = 1.0F / 255.0F;
    return {
        color.Red * scale,
        color.Green * scale,
        color.Blue * scale,
        color.Alpha * scale};
}

bool VoxelViewportState::Replace(
    std::string name,
    const Voxel::VoxelModel& model,
    const Mesh::MeshData& mesh)
{
    const Voxel::VoxelGrid* grid = model.GetGrid(0U);
    if (grid == nullptr || mesh.Empty())
    {
        return false;
    }

    name_ = std::move(name);
    statistics_ = {
        grid->Width(),
        grid->Height(),
        grid->Depth(),
        grid->OccupiedVoxelCount(),
        mesh.VertexCount(),
        mesh.TriangleCount()};
    hasModel_ = true;
    return true;
}

void VoxelViewportState::Clear() noexcept
{
    name_.clear();
    statistics_ = {};
    hasModel_ = false;
}

bool VoxelViewportState::HasModel() const noexcept
{
    return hasModel_;
}

const std::string& VoxelViewportState::Name() const noexcept
{
    return name_;
}

const VoxelViewportStatistics& VoxelViewportState::Statistics() const noexcept
{
    return statistics_;
}

void VoxelViewportState::SetGridVisible(const bool visible) noexcept
{
    gridVisible_ = visible;
}

void VoxelViewportState::SetAxesVisible(const bool visible) noexcept
{
    axesVisible_ = visible;
}

void VoxelViewportState::SetBackground(
    const ViewportBackground background) noexcept
{
    background_ = background;
}

bool VoxelViewportState::IsGridVisible() const noexcept
{
    return gridVisible_;
}

bool VoxelViewportState::AreAxesVisible() const noexcept
{
    return axesVisible_;
}

ViewportBackground VoxelViewportState::Background() const noexcept
{
    return background_;
}

std::array<float, 4> VoxelViewportState::BackgroundColor() const noexcept
{
    switch (background_)
    {
    case ViewportBackground::Neutral:
        return {0.18F, 0.19F, 0.21F, 1.0F};
    case ViewportBackground::Light:
        return {0.62F, 0.64F, 0.68F, 1.0F};
    case ViewportBackground::Dark:
    default:
        return {0.055F, 0.070F, 0.095F, 1.0F};
    }
}

} // namespace VoxelForge::Editor
