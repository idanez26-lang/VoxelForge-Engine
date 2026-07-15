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

} // namespace VoxelForge::Editor
