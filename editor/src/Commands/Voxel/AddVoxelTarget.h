#pragma once

#include "VoxelSelection/VoxelRaycast.h"

#include <optional>

namespace VoxelForge::Voxel
{
class VoxelGrid;
}

namespace VoxelForge::Asset::Voxel
{
class VoxelDocument;
}

namespace VoxelForge::Editor
{

enum class AddVoxelTargetStatus
{
    Available,
    NoSelection,
    MissingGrid,
    SelectedOutsideGrid,
    SelectedVoxelEmpty,
    StaleSelection,
    InvalidFace,
    OutsideGrid,
    DestinationOccupied
};

struct AddVoxelTarget final
{
    AddVoxelTargetStatus Status = AddVoxelTargetStatus::NoSelection;
    std::optional<VoxelCoordinates> Coordinates;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Status == AddVoxelTargetStatus::Available &&
            Coordinates.has_value();
    }
};

[[nodiscard]] AddVoxelTarget FindAddVoxelTarget(
    const Voxel::VoxelGrid* grid,
    const std::optional<VoxelRaycastHit>& selection) noexcept;

[[nodiscard]] AddVoxelTarget FindAddVoxelTarget(
    const Asset::Voxel::VoxelDocument& document,
    const std::optional<VoxelRaycastHit>& selection) noexcept;

[[nodiscard]] const char* AddVoxelTargetStatusMessage(
    AddVoxelTargetStatus status) noexcept;

} // namespace VoxelForge::Editor
