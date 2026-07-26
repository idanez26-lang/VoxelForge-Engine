#include "SmartTools/SmartToolSession.h"

#include <utility>

namespace VoxelForge::Editor
{
const SmartToolSessionState& SmartToolSession::State() const noexcept { return state_; }
void SmartToolSession::SetState(SmartToolSessionState state)
{ state_ = std::move(state); }
void SmartToolSession::SetActiveGeometry(const SmartGeometry geometry) noexcept
{ state_.ToolActive = true; state_.Geometry = geometry; }
void SmartToolSession::SetActiveProfileUuid(std::string uuid)
{ state_.ActiveProfileUuid = std::move(uuid); }
void SmartToolSession::SetPaletteIndex(
    const std::optional<std::size_t> paletteIndex) noexcept
{ state_.PaletteIndex = paletteIndex; }
void SmartToolSession::SetBrush(SmartBrushState brush) noexcept
{ state_.Brush = brush; }
void SmartToolSession::SetAction(const SmartAction action) noexcept
{ state_.Action = action; }
void SmartToolSession::SetMode(const SmartBrushMode mode) noexcept
{ state_.Mode = mode; }
void SmartToolSession::SetSelection(
    std::vector<Asset::Voxel::VoxelPosition> selection)
{ state_.Selection = std::move(selection); }
void SmartToolSession::SetWorkplane(
    const std::optional<SmartBrushPlacement> workplane) noexcept
{ state_.Workplane = workplane; }

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
