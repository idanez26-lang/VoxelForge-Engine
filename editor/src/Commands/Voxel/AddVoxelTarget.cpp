#include "AddVoxelTarget.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"
#include "VoxelForge/Voxel/VoxelGrid.h"

#include <cstdint>

namespace VoxelForge::Editor
{
namespace
{
template<typename OccupiedQuery>
AddVoxelTarget ResolveAddVoxelTarget(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t depth,
    const VoxelRaycastHit& selection,
    OccupiedQuery&& occupied) noexcept
{
    const VoxelCoordinates source = selection.Coordinates;
    if (source.X >= width || source.Y >= height || source.Z >= depth)
        return {AddVoxelTargetStatus::SelectedOutsideGrid, std::nullopt};
    if (!occupied(source))
        return {AddVoxelTargetStatus::SelectedVoxelEmpty, std::nullopt};

    std::int64_t x = source.X;
    std::int64_t y = source.Y;
    std::int64_t z = source.Z;
    switch (selection.Face)
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
        x >= static_cast<std::int64_t>(width) ||
        y >= static_cast<std::int64_t>(height) ||
        z >= static_cast<std::int64_t>(depth))
    {
        return {AddVoxelTargetStatus::OutsideGrid, std::nullopt};
    }

    const VoxelCoordinates destination{
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(z)};
    if (occupied(destination))
        return {AddVoxelTargetStatus::DestinationOccupied, destination};
    return {AddVoxelTargetStatus::Available, destination};
}
}

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
    return ResolveAddVoxelTarget(
        grid->Width(), grid->Height(), grid->Depth(), *selection,
        [grid](const VoxelCoordinates coordinates) noexcept
        {
            const Voxel::Voxel* voxel =
                grid->Get(coordinates.X, coordinates.Y, coordinates.Z);
            return voxel != nullptr && voxel->IsOccupied();
        });
}

AddVoxelTarget FindAddVoxelTarget(
    const Asset::Voxel::VoxelDocument& document,
    const std::optional<VoxelRaycastHit>& selection) noexcept
{
    if (!selection)
        return {AddVoxelTargetStatus::NoSelection, std::nullopt};
    if (selection->DocumentRevision != document.GetRevision())
        return {AddVoxelTargetStatus::StaleSelection, std::nullopt};
    const auto dimensions = document.GetDimensions(selection->SubModelIndex);
    if (!dimensions)
        return {AddVoxelTargetStatus::MissingGrid, std::nullopt};
    return ResolveAddVoxelTarget(
        dimensions->X, dimensions->Y, dimensions->Z, *selection,
        [&document, modelIndex = selection->SubModelIndex](
            const VoxelCoordinates coordinates) noexcept
        {
            return document.HasVoxel({
                static_cast<std::int32_t>(coordinates.X),
                static_cast<std::int32_t>(coordinates.Y),
                static_cast<std::int32_t>(coordinates.Z)}, modelIndex);
        });
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
        return "No voxel document model or legacy grid is available.";
    case AddVoxelTargetStatus::SelectedOutsideGrid:
        return "The selected voxel is outside the current grid.";
    case AddVoxelTargetStatus::SelectedVoxelEmpty:
        return "The selected voxel is no longer occupied.";
    case AddVoxelTargetStatus::StaleSelection:
        return "The selected voxel belongs to an older document revision.";
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
