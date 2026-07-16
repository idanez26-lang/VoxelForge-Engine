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
    return !left || (left->Coordinates == right->Coordinates &&
        left->SubModelIndex == right->SubModelIndex);
}
}

const char* VoxelPickingInteractionStateName(
    const VoxelPickingInteractionState state) noexcept
{
    switch (state)
    {
    case VoxelPickingInteractionState::Unavailable: return "Unavailable";
    case VoxelPickingInteractionState::OutsideViewport: return "Outside viewport";
    case VoxelPickingInteractionState::NoDocument: return "No document";
    case VoxelPickingInteractionState::NoHit: return "No hit";
    case VoxelPickingInteractionState::Hit: return "Hit";
    case VoxelPickingInteractionState::CameraInteraction: return "Camera interaction";
    case VoxelPickingInteractionState::Blocked: return "Blocked";
    }
    return "Unavailable";
}

bool VoxelSelectionState::SetHovered(
    std::optional<VoxelRaycastHit> hit) noexcept
{
    return SetHovered(
        hit ? VoxelPickingInteractionState::Hit
            : VoxelPickingInteractionState::NoHit,
        std::move(hit));
}

bool VoxelSelectionState::SetHovered(
    VoxelPickingInteractionState interactionState,
    std::optional<VoxelRaycastHit> hit) noexcept
{
    if (interactionState != VoxelPickingInteractionState::Hit)
        hit.reset();
    else if (!hit)
        interactionState = VoxelPickingInteractionState::NoHit;
    const bool changed = !SameVoxel(hovered_, hit);
    hovered_ = std::move(hit);
    interactionState_ = interactionState;
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
    interactionState_ = VoxelPickingInteractionState::Unavailable;
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

VoxelPickingInteractionState
VoxelSelectionState::InteractionState() const noexcept
{
    return interactionState_;
}

} // namespace VoxelForge::Editor
