#pragma once

#include "VoxelRaycast.h"

#include <optional>

namespace VoxelForge::Editor
{

class VoxelSelectionState final
{
public:
    [[nodiscard]] bool SetHovered(std::optional<VoxelRaycastHit> hit) noexcept;
    [[nodiscard]] bool SelectHovered() noexcept;
    [[nodiscard]] bool ClearSelection() noexcept;
    [[nodiscard]] bool Clear() noexcept;

    [[nodiscard]] const std::optional<VoxelRaycastHit>& Hovered() const noexcept;
    [[nodiscard]] const std::optional<VoxelRaycastHit>& Selected() const noexcept;

private:
    std::optional<VoxelRaycastHit> hovered_;
    std::optional<VoxelRaycastHit> selected_;
};

} // namespace VoxelForge::Editor
