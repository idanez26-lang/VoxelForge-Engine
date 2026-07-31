#include "SmartTools/PencilCompactPlan.h"

#include <limits>
#include <utility>

namespace VoxelForge::Editor
{
PencilCompactPlan::PencilCompactPlan(SmartBrushCompactCacheKey cacheKey,
    std::shared_ptr<const SmartBrushCompactFootprint> footprint,
    const SmartBrushPlacement placement, const Asset::Voxel::VoxelPosition anchor,
    const SmartBrushBounds bounds, const std::uint64_t planId,
    const bool insideDocument, const SmartAction action,
    const std::size_t paletteIndex, const std::uint64_t documentGeneration,
    const std::uint64_t documentRevision) noexcept
    : cacheKey_(std::move(cacheKey)), footprint_(std::move(footprint)),
      placement_(placement), anchor_(anchor), bounds_(bounds), planId_(planId),
      insideDocument_(insideDocument), action_(action), paletteIndex_(paletteIndex),
      documentGeneration_(documentGeneration), documentRevision_(documentRevision)
{
}

const SmartBrushCompactCacheKey& PencilCompactPlan::CacheKey() const noexcept
{ return cacheKey_; }

const SmartBrushCompactDescriptor& PencilCompactPlan::Descriptor() const noexcept
{ return footprint_->Descriptor(); }

const SmartBrushPlacement& PencilCompactPlan::Placement() const noexcept
{ return placement_; }
SmartAction PencilCompactPlan::Action() const noexcept { return action_; }
std::size_t PencilCompactPlan::PaletteIndex() const noexcept { return paletteIndex_; }
std::uint64_t PencilCompactPlan::DocumentGeneration() const noexcept
{ return documentGeneration_; }
std::uint64_t PencilCompactPlan::DocumentRevision() const noexcept
{ return documentRevision_; }

const SmartBrushBounds& PencilCompactPlan::Bounds() const noexcept
{ return bounds_; }

std::size_t PencilCompactPlan::ExactVoxelCount() const noexcept
{ return footprint_->ExactVoxelCount(); }

std::uint64_t PencilCompactPlan::PlanId() const noexcept { return planId_; }

bool PencilCompactPlan::IsInsideDocument() const noexcept { return insideDocument_; }

std::size_t PencilCompactPlan::MaterializedPositionCount() const noexcept
{ return footprint_->MaterializedPositionCount(); }

PencilCompactCellState PencilCompactPlan::ResolveCell(
    const PencilCompactCellState before) const noexcept
{
    PencilCompactCellState after = before;
    switch (action_)
    {
    case SmartAction::Add:
        after.Exists = true;
        after.PaletteIndex = static_cast<std::uint8_t>(paletteIndex_);
        break;
    case SmartAction::Erase:
        if (before.Exists)
        {
            after.Exists = false;
            after.PaletteIndex = 0U;
        }
        break;
    case SmartAction::Paint:
        if (before.Exists)
            after.PaletteIndex = static_cast<std::uint8_t>(paletteIndex_);
        break;
    default:
        break;
    }
    return after;
}

PencilCompactPlan::Iterator PencilCompactPlan::Iterate() const noexcept
{ return Iterator(*this); }

PencilCompactPlan::Iterator::Iterator(const PencilCompactPlan& plan) noexcept
    : plan_(&plan), offsets_(plan.footprint_->Iterate())
{
}

bool PencilCompactPlan::Iterator::Next(Asset::Voxel::VoxelPosition& position) noexcept
{
    Asset::Voxel::VoxelPosition offset;
    if (!plan_ || !offsets_.Next(offset)) return false;
    const std::int64_t x = static_cast<std::int64_t>(plan_->anchor_.X) + offset.X;
    const std::int64_t y = static_cast<std::int64_t>(plan_->anchor_.Y) + offset.Y;
    const std::int64_t z = static_cast<std::int64_t>(plan_->anchor_.Z) + offset.Z;
    if (x < std::numeric_limits<std::int32_t>::min() ||
        x > std::numeric_limits<std::int32_t>::max() ||
        y < std::numeric_limits<std::int32_t>::min() ||
        y > std::numeric_limits<std::int32_t>::max() ||
        z < std::numeric_limits<std::int32_t>::min() ||
        z > std::numeric_limits<std::int32_t>::max())
        return false;
    position = {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
        static_cast<std::int32_t>(z)};
    return true;
}
} // namespace VoxelForge::Editor
