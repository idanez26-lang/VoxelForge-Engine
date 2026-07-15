#include "VoxelSelectionState.h"

#include <utility>

namespace VoxelForge::Editor
{
namespace
{
bool SameVoxel(
    const std::optional<VoxelRaycastHit>& left,
    const std::optional<VoxelRaycastHit>& right) noexcept
{
    if (left.has_value() != right.has_value()) return false;
    return !left || left->Coordinates == right->Coordinates;
}
}

bool VoxelSelectionState::SetHovered(
    std::optional<VoxelRaycastHit> hit) noexcept
{
    const bool changed = !SameVoxel(hovered_, hit);
    hovered_ = std::move(hit);
    return changed;
}

bool VoxelSelectionState::SelectHovered() noexcept
{
    if (SameVoxel(selected_, hovered_))
    {
        selected_ = hovered_;
        return false;
    }
    selected_ = hovered_;
    return true;
}

bool VoxelSelectionState::ClearSelection() noexcept
{
    if (!selected_) return false;
    selected_.reset();
    return true;
}

bool VoxelSelectionState::Clear() noexcept
{
    const bool changed = hovered_.has_value() || selected_.has_value();
    hovered_.reset();
    selected_.reset();
    return changed;
}

const std::optional<VoxelRaycastHit>&
VoxelSelectionState::Hovered() const noexcept
{
    return hovered_;
}

const std::optional<VoxelRaycastHit>&
VoxelSelectionState::Selected() const noexcept
{
    return selected_;
}

} // namespace VoxelForge::Editor
