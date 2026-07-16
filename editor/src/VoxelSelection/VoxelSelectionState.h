#pragma once

#include "VoxelRaycast.h"

#include <optional>

namespace VoxelForge::Editor
{

enum class VoxelPickingInteractionState
{
    Unavailable,
    OutsideViewport,
    NoDocument,
    NoHit,
    Hit,
    CameraInteraction,
    Blocked
};

[[nodiscard]] const char* VoxelPickingInteractionStateName(
    VoxelPickingInteractionState state) noexcept;

class VoxelSelectionState final
{
public:
    [[nodiscard]] bool SetHovered(std::optional<VoxelRaycastHit> hit) noexcept;
    [[nodiscard]] bool SetHovered(
        VoxelPickingInteractionState interactionState,
        std::optional<VoxelRaycastHit> hit = std::nullopt) noexcept;
    [[nodiscard]] bool SelectHovered() noexcept;
    [[nodiscard]] bool ClearSelection() noexcept;
    [[nodiscard]] bool Clear() noexcept;

    [[nodiscard]] const std::optional<VoxelRaycastHit>& Hovered() const noexcept;
    [[nodiscard]] const std::optional<VoxelRaycastHit>& Selected() const noexcept;
    [[nodiscard]] VoxelPickingInteractionState InteractionState() const noexcept;

private:
    std::optional<VoxelRaycastHit> hovered_;
    std::optional<VoxelRaycastHit> selected_;
    VoxelPickingInteractionState interactionState_ =
        VoxelPickingInteractionState::Unavailable;
};

} // namespace VoxelForge::Editor
