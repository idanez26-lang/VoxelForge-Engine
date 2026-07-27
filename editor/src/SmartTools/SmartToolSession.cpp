#include "SmartTools/SmartToolSession.h"

#include <utility>

namespace VoxelForge::Editor
{
namespace
{
bool SamePlacement(
    const std::optional<SmartBrushPlacement>& left,
    const std::optional<SmartBrushPlacement>& right) noexcept
{
    if (left.has_value() != right.has_value()) return false;
    return !left || (left->Target == right->Target &&
        left->Normal == right->Normal);
}
}

const SmartToolSessionState& SmartToolSession::State() const noexcept { return state_; }
void SmartToolSession::SetState(SmartToolSessionState state)
{
    state_ = std::move(state);
    Clear();
}
void SmartToolSession::SetActiveGeometry(const SmartGeometry geometry) noexcept
{
    if (state_.ToolActive && state_.Geometry == geometry) return;
    state_.ToolActive = true;
    state_.Geometry = geometry;
    Clear();
}
void SmartToolSession::SetActiveProfileUuid(std::string uuid)
{
    if (state_.ActiveProfileUuid == uuid) return;
    state_.ActiveProfileUuid = std::move(uuid);
    Clear();
}
void SmartToolSession::SetPaletteIndex(
    const std::optional<std::size_t> paletteIndex) noexcept
{
    if (state_.PaletteIndex == paletteIndex) return;
    state_.PaletteIndex = paletteIndex;
    Clear();
}
void SmartToolSession::SetBrush(SmartBrushState brush) noexcept
{
    if (state_.Brush == brush) return;
    state_.Brush = brush;
    Clear();
}
void SmartToolSession::SetAction(const SmartAction action) noexcept
{
    if (state_.Action == action) return;
    state_.Action = action;
    Clear();
}
void SmartToolSession::SetMode(const SmartBrushMode mode) noexcept
{
    if (state_.Mode == mode) return;
    state_.Mode = mode;
    Clear();
}
void SmartToolSession::SetSelection(
    std::vector<Asset::Voxel::VoxelPosition> selection)
{
    if (state_.Selection == selection) return;
    state_.Selection = std::move(selection);
    Clear();
}
void SmartToolSession::SetWorkplane(
    const std::optional<SmartBrushPlacement> workplane) noexcept
{
    if (SamePlacement(state_.Workplane, workplane)) return;
    state_.Workplane = workplane;
    Clear();
}

bool SmartToolSession::HasPlanFor(const SmartToolRequestKey& key) const noexcept
{
    return plan_ != nullptr && key_ && *key_ == key;
}

void SmartToolSession::SetPlan(SmartToolPlanPtr plan) noexcept
{
    if (plan == nullptr)
    {
        Clear();
        return;
    }
    key_ = plan->CacheKey();
    plan_ = std::move(plan);
}

void SmartToolSession::Clear() noexcept
{
    key_.reset();
    plan_.reset();
}

SmartToolPlanPtr SmartToolSession::PlanForPreview() const noexcept { return plan_; }
SmartToolPlanPtr SmartToolSession::PlanForCommit() const noexcept { return plan_; }
} // namespace VoxelForge::Editor
