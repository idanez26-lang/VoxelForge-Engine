#pragma once

#include "SmartTools/SmartToolPlan.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{
// All interaction state is by value. In particular, this object never owns or
// retains a VoxelDocument, renderer, viewport, or ImGui handle.
struct SmartToolSessionState final
{
    bool ToolActive = false;
    SmartGeometry Geometry = SmartGeometry::Pencil;
    std::optional<SmartToolMode> ToolMode;
    std::string ActiveProfileUuid;
    std::optional<std::size_t> PaletteIndex;
    SmartBrushState Brush{};
    SmartAction Action = SmartAction::Add;
    SmartBrushMode Mode = SmartBrushMode::Add;
    std::vector<Asset::Voxel::VoxelPosition> Selection;
    std::optional<SmartBrushPlacement> Workplane;
};

class SmartToolSession final
{
public:
    [[nodiscard]] const SmartToolSessionState& State() const noexcept;
    void SetState(SmartToolSessionState state);
    void SetActiveGeometry(SmartGeometry geometry) noexcept;
    void SetToolMode(std::optional<SmartToolMode> mode) noexcept;
    void SetActiveProfileUuid(std::string uuid);
    void SetPaletteIndex(std::optional<std::size_t> paletteIndex) noexcept;
    void SetBrush(SmartBrushState brush) noexcept;
    void SetAction(SmartAction action) noexcept;
    void SetMode(SmartBrushMode mode) noexcept;
    void SetSelection(std::vector<Asset::Voxel::VoxelPosition> selection);
    void SetWorkplane(std::optional<SmartBrushPlacement> workplane) noexcept;

    [[nodiscard]] bool HasPlanFor(const SmartToolRequestKey& key) const noexcept;
    void SetPlan(SmartToolPlanPtr plan) noexcept;
    void Clear() noexcept;

    [[nodiscard]] SmartToolPlanPtr PlanForPreview() const noexcept;
    [[nodiscard]] SmartToolPlanPtr PlanForCommit() const noexcept;

private:
    SmartToolSessionState state_;
    std::optional<SmartToolRequestKey> key_;
    SmartToolPlanPtr plan_;
};
} // namespace VoxelForge::Editor
