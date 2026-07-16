#include "AddVoxelTarget.h"

#include "VoxelForge/Voxel/VoxelGrid.h"

#include <cstdint>

namespace VoxelForge::Editor
{

AddVoxelTarget FindAddVoxelTarget(
    const Voxel::VoxelGrid* grid,
    const std::optional<VoxelRaycastHit>& selection) noexcept
{
    if (!selection)
    {
        return {AddVoxelTargetStatus::NoSelection, std::nullopt};
    }
    if (grid == nullptr)
    {
        return {AddVoxelTargetStatus::MissingGrid, std::nullopt};
    }

    const VoxelCoordinates source = selection->Coordinates;
    const Voxel::Voxel* sourceVoxel = grid->Get(source.X, source.Y, source.Z);
    if (sourceVoxel == nullptr)
    {
        return {AddVoxelTargetStatus::SelectedOutsideGrid, std::nullopt};
    }
    if (!sourceVoxel->IsOccupied())
    {
        return {AddVoxelTargetStatus::SelectedVoxelEmpty, std::nullopt};
    }

    std::int64_t x = source.X;
    std::int64_t y = source.Y;
    std::int64_t z = source.Z;
    switch (selection->Face)
    {
    case VoxelHitFace::NegativeX: --x; break;
    case VoxelHitFace::PositiveX: ++x; break;
    case VoxelHitFace::NegativeY: --y; break;
    case VoxelHitFace::PositiveY: ++y; break;
    case VoxelHitFace::NegativeZ: --z; break;
    case VoxelHitFace::PositiveZ: ++z; break;
    case VoxelHitFace::None:
        return {AddVoxelTargetStatus::InvalidFace, std::nullopt};
    }

    if (x < 0 || y < 0 || z < 0 ||
        x >= static_cast<std::int64_t>(grid->Width()) ||
        y >= static_cast<std::int64_t>(grid->Height()) ||
        z >= static_cast<std::int64_t>(grid->Depth()))
    {
        return {AddVoxelTargetStatus::OutsideGrid, std::nullopt};
    }

    const VoxelCoordinates destination{
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(z)};
    const Voxel::Voxel* destinationVoxel =
        grid->Get(destination.X, destination.Y, destination.Z);
    if (destinationVoxel == nullptr)
    {
        return {AddVoxelTargetStatus::OutsideGrid, std::nullopt};
    }
    if (destinationVoxel->IsOccupied())
    {
        return {AddVoxelTargetStatus::DestinationOccupied, destination};
    }
    return {AddVoxelTargetStatus::Available, destination};
}

const char* AddVoxelTargetStatusMessage(
    const AddVoxelTargetStatus status) noexcept
{
    switch (status)
    {
    case AddVoxelTargetStatus::Available:
        return "Ready to add a voxel.";
    case AddVoxelTargetStatus::NoSelection:
        return "Select a voxel face first.";
    case AddVoxelTargetStatus::MissingGrid:
        return "No voxel grid is available.";
    case AddVoxelTargetStatus::SelectedOutsideGrid:
        return "The selected voxel is outside the current grid.";
    case AddVoxelTargetStatus::SelectedVoxelEmpty:
        return "The selected voxel is no longer occupied.";
    case AddVoxelTargetStatus::InvalidFace:
        return "Cannot add from an inside hit.";
    case AddVoxelTargetStatus::OutsideGrid:
        return "Cannot add outside the current grid.";
    case AddVoxelTargetStatus::DestinationOccupied:
        return "The adjacent cell is already occupied.";
    }
    return "Cannot add a voxel here.";
}

} // namespace VoxelForge::Editor
