#include "SmartTools/SmartToolStroke.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

[[nodiscard]] std::int64_t Abs64(const std::int64_t value) noexcept
{
    return value < 0 ? -value : value;
}

[[nodiscard]] std::int32_t InterpolateCoordinate(const std::int32_t from,
    const std::int64_t delta, const std::int64_t step,
    const std::int64_t count) noexcept
{
    // Round halves away from zero. This is explicit and platform-independent.
    const std::int64_t numerator = static_cast<std::int64_t>(delta) * step;
    const std::int64_t rounded = numerator >= 0
        ? (numerator + count / 2) / count
        : -((-numerator + count / 2) / count);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(from) + rounded);
}

[[nodiscard]] bool SameNormal(const Position left, const Position right) noexcept
{
    return left == right;
}
}

std::vector<Position> SmartToolStrokeInterpolator::Sample(
    const Position from, const Position to)
{
    const std::int64_t deltaX = static_cast<std::int64_t>(to.X) - from.X;
    const std::int64_t deltaY = static_cast<std::int64_t>(to.Y) - from.Y;
    const std::int64_t deltaZ = static_cast<std::int64_t>(to.Z) - from.Z;
    const std::int64_t steps = std::max({Abs64(deltaX), Abs64(deltaY), Abs64(deltaZ)});
    if (steps == 0) return {from};

    std::vector<Position> samples;
    samples.reserve(static_cast<std::size_t>(steps) + 1U);
    for (std::int64_t step = 0; step <= steps; ++step)
    {
        const Position position{
            InterpolateCoordinate(from.X, deltaX, step, steps),
            InterpolateCoordinate(from.Y, deltaY, step, steps),
            InterpolateCoordinate(from.Z, deltaZ, step, steps)};
        if (samples.empty() || samples.back() != position) samples.push_back(position);
    }
    return samples;
}

std::size_t SmartToolStroke::PositionHash::operator()(
    const Position position) const noexcept
{
    const auto x = static_cast<std::uint32_t>(position.X);
    const auto y = static_cast<std::uint32_t>(position.Y);
    const auto z = static_cast<std::uint32_t>(position.Z);
    return static_cast<std::size_t>(x) ^
        (static_cast<std::size_t>(y) << 11U) ^
        (static_cast<std::size_t>(z) << 22U);
}

bool SmartToolStroke::Begin(SmartToolStrokeContext context, const SmartAction action,
    const Position target, const Position normal,
    const SmartToolStrokeSurfacePolicy surfacePolicy)
{
    Cancel();
    if (!context.ReadSourceVoxel ||
        (action != SmartAction::Add && action != SmartAction::Paint &&
         action != SmartAction::Erase) ||
        (surfacePolicy == SmartToolStrokeSurfacePolicy::LockPencilSurface &&
         !IsUnitAxisNormal(normal)))
        return false;
    context_ = std::move(context);
    action_ = action;
    surfacePolicy_ = surfacePolicy;
    active_ = true;
    suspended_ = false;
    if (surfacePolicy_ == SmartToolStrokeSurfacePolicy::LockPencilSurface)
        LockPencilSurface(target, normal);
    SetAnchor(target, normal);
    ++revision_;
    return true;
}

void SmartToolStroke::Cancel() noexcept
{
    context_ = {};
    cells_.clear();
    changesCacheValid_ = false;
    lastTarget_.reset();
    lastNormal_.reset();
    pencilSurfaceNormal_.reset();
    pencilSurfaceCoordinate_ = 0;
    surfacePolicy_ = SmartToolStrokeSurfacePolicy::Unlocked;
    active_ = false;
    suspended_ = false;
    ++revision_;
}

void SmartToolStroke::Suspend() noexcept
{
    if (!active_ || suspended_) return;
    suspended_ = true;
    lastTarget_.reset();
    lastNormal_.reset();
    ++revision_;
}

bool SmartToolStroke::IsActive() const noexcept { return active_; }
bool SmartToolStroke::IsSuspended() const noexcept { return suspended_; }
SmartAction SmartToolStroke::Action() const noexcept { return action_; }
const SmartToolStrokeContext& SmartToolStroke::Context() const noexcept { return context_; }
std::uint64_t SmartToolStroke::Revision() const noexcept { return revision_; }

std::vector<Position> SmartToolStroke::Advance(
    const Position target, const Position normal)
{
    if (!active_) return {};
    Position constrainedTarget = target;
    bool startsNewSurfaceSegment = false;
    if (surfacePolicy_ == SmartToolStrokeSurfacePolicy::LockPencilSurface)
    {
        if (!IsUnitAxisNormal(normal)) return {};
        if (!pencilSurfaceNormal_ || *pencilSurfaceNormal_ != normal)
        {
            LockPencilSurface(target, normal);
            startsNewSurfaceSegment = true;
        }
        else
        {
            constrainedTarget = ConstrainToPencilSurface(target);
        }
    }
    if (!suspended_ && lastTarget_ && lastNormal_ &&
        *lastTarget_ == constrainedTarget && SameNormal(*lastNormal_, normal))
        return {};
    std::vector<Position> samples;
    if (!startsNewSurfaceSegment && !suspended_ && lastTarget_ && lastNormal_ &&
        SameNormal(*lastNormal_, normal))
    {
        samples = SmartToolStrokeInterpolator::Sample(
            *lastTarget_, constrainedTarget);
        if (!samples.empty()) samples.erase(samples.begin());
    }
    // A duplicate first sample is intentionally omitted; every cell is then
    // planned exactly once per movement segment. If the point did not move,
    // the early return above prevents a cache revision or preview rebuild.
    if (samples.empty()) samples.push_back(constrainedTarget);
    suspended_ = false;
    SetAnchor(constrainedTarget, normal);
    ++revision_;
    return samples;
}

SmartToolVoxelState SmartToolStroke::ReadVoxel(const Position position) const
{
    const auto found = cells_.find(position);
    if (found != cells_.end())
        return {found->second.Change.ExistsAfter, found->second.Change.PaletteIndexAfter};
    return context_.ReadSourceVoxel ? context_.ReadSourceVoxel(position)
                                    : SmartToolVoxelState{};
}

bool SmartToolStroke::Accumulate(const SmartToolPlan& plan)
{
    if (!active_ || !MatchesContext(plan) || plan.Action() != action_) return false;
    changesCacheValid_ = false;
    bool changed = false;
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        if (!cell.HasChange()) continue;
        const SmartToolVoxelState expectedBefore = ReadVoxel(cell.WorldPosition);
        if (expectedBefore != cell.Before)
        {
            // LOT 4b : sortie sur desaccord APRES avoir deja fusionne des
            // cellules. Sans ce bump, revision_ mentirait sur un trait
            // reellement modifie, et les caches indexes sur Revision()
            // (dont la preview exacte) afficheraient un etat perime.
            if (changed) ++revision_;
            return false;
        }

        MergeChange(cells_, {context_.SubModelIndex, cell.WorldPosition,
            cell.Before.Exists, cell.Before.PaletteIndex,
            cell.After.Exists, cell.After.PaletteIndex});
        changed = true;
    }
    if (changed) ++revision_;
    return true;
}

bool SmartToolStroke::ReplaceWithPlan(const SmartToolPlan& plan)
{
    if (!active_ || !MatchesContext(plan) || plan.Action() != action_) return false;
    std::unordered_map<Position, AccumulatedCell, PositionHash> replacement;
    replacement.reserve(plan.Cells().size());
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        if (!cell.HasChange()) continue;
        const SmartToolVoxelState sourceBefore = context_.ReadSourceVoxel
            ? context_.ReadSourceVoxel(cell.WorldPosition) : SmartToolVoxelState{};
        if (sourceBefore != cell.Before) return false;
        MergeChange(replacement, {context_.SubModelIndex, cell.WorldPosition,
            cell.Before.Exists, cell.Before.PaletteIndex,
            cell.After.Exists, cell.After.PaletteIndex});
    }
    cells_ = std::move(replacement);
    changesCacheValid_ = false;
    ++revision_;
    return true;
}

std::span<const Asset::Voxel::VoxelDocumentChange>
    SmartToolStroke::ChangesView() const
{
    if (changesCacheValid_ && changesCacheRevision_ == revision_)
        return changesCache_;
    changesCache_.clear();
    changesCache_.reserve(cells_.size());
    for (const auto& [position, cell] : cells_)
    {
        static_cast<void>(position);
        changesCache_.push_back(cell.Change);
    }
    std::sort(changesCache_.begin(), changesCache_.end(),
        [](const Asset::Voxel::VoxelDocumentChange& left,
            const Asset::Voxel::VoxelDocumentChange& right)
        {
            if (left.Position.X != right.Position.X) return left.Position.X < right.Position.X;
            if (left.Position.Y != right.Position.Y) return left.Position.Y < right.Position.Y;
            return left.Position.Z < right.Position.Z;
        });
    changesCacheRevision_ = revision_;
    changesCacheValid_ = true;
    return changesCache_;
}

std::vector<Asset::Voxel::VoxelDocumentChange> SmartToolStroke::Changes() const
{
    const std::span<const Asset::Voxel::VoxelDocumentChange> view = ChangesView();
    return {view.begin(), view.end()};
}

std::vector<Asset::Voxel::VoxelDocumentChange> SmartToolStroke::PreviewChanges(
    const SmartToolPlan& nextPlan) const
{
    if (!MatchesContext(nextPlan) || nextPlan.Action() != action_) return {};
    auto merged = cells_;
    for (const SmartToolPlanCell& cell : nextPlan.Cells())
    {
        if (!cell.HasChange()) continue;
        MergeChange(merged, {context_.SubModelIndex, cell.WorldPosition,
            cell.Before.Exists, cell.Before.PaletteIndex,
            cell.After.Exists, cell.After.PaletteIndex});
    }
    std::vector<Asset::Voxel::VoxelDocumentChange> changes;
    changes.reserve(merged.size());
    for (const auto& [position, cell] : merged)
    {
        static_cast<void>(position);
        changes.push_back(cell.Change);
    }
    std::sort(changes.begin(), changes.end(),
        [](const Asset::Voxel::VoxelDocumentChange& left,
            const Asset::Voxel::VoxelDocumentChange& right)
        {
            if (left.Position.X != right.Position.X) return left.Position.X < right.Position.X;
            if (left.Position.Y != right.Position.Y) return left.Position.Y < right.Position.Y;
            return left.Position.Z < right.Position.Z;
        });
    return changes;
}

bool SmartToolStroke::HasChanges() const noexcept { return !cells_.empty(); }

bool SmartToolStroke::MatchesContext(const SmartToolPlan& plan) const noexcept
{
    const SmartToolRequestKey& key = plan.CacheKey();
    return key.SourceIdentity == context_.DocumentIdentity &&
        key.SourceRevision == context_.DocumentRevision &&
        key.SourceGeneration == context_.DocumentGeneration &&
        key.SourceSubModelIndex == context_.SubModelIndex;
}

void SmartToolStroke::MergeChange(
    std::unordered_map<Position, AccumulatedCell, PositionHash>& cells,
    const Asset::Voxel::VoxelDocumentChange& change)
{
    const auto [iterator, inserted] = cells.try_emplace(change.Position,
        AccumulatedCell{change});
    if (!inserted)
    {
        iterator->second.Change.ExistsAfter = change.ExistsAfter;
        iterator->second.Change.PaletteIndexAfter = change.PaletteIndexAfter;
    }
    const auto& merged = iterator->second.Change;
    if (merged.ExistedBefore == merged.ExistsAfter &&
        (!merged.ExistedBefore ||
         merged.PaletteIndexBefore == merged.PaletteIndexAfter))
        cells.erase(iterator);
}

void SmartToolStroke::SetAnchor(const Position target, const Position normal) noexcept
{
    lastTarget_ = target;
    lastNormal_ = normal;
}

bool SmartToolStroke::IsUnitAxisNormal(const Position normal) noexcept
{
    const auto isAxisComponent = [](const std::int32_t value) noexcept
    {
        return value >= -1 && value <= 1;
    };
    return isAxisComponent(normal.X) && isAxisComponent(normal.Y) &&
        isAxisComponent(normal.Z) &&
        normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z == 1;
}

void SmartToolStroke::LockPencilSurface(
    const Position target, const Position normal) noexcept
{
    pencilSurfaceNormal_ = normal;
    pencilSurfaceCoordinate_ = normal.X != 0
        ? target.X : normal.Y != 0 ? target.Y : target.Z;
}

Position SmartToolStroke::ConstrainToPencilSurface(
    Position target) const noexcept
{
    if (!pencilSurfaceNormal_) return target;
    if (pencilSurfaceNormal_->X != 0)
        target.X = pencilSurfaceCoordinate_;
    else if (pencilSurfaceNormal_->Y != 0)
        target.Y = pencilSurfaceCoordinate_;
    else
        target.Z = pencilSurfaceCoordinate_;
    return target;
}
} // namespace VoxelForge::Editor
