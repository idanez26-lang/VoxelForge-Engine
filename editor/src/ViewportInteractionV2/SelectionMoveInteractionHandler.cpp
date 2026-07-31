#include "SelectionMoveInteractionHandler.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace VoxelForge::Editor::InteractionV2
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

[[nodiscard]] bool PositionLess(
    const Position left, const Position right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

[[nodiscard]] SelectionBounds BoundsOf(
    const std::span<const Position> positions) noexcept
{
    if (positions.empty()) return {};
    Position minimum = positions.front();
    Position maximum = positions.front();
    for (const Position position : positions.subspan(1U))
    {
        minimum.X = std::min(minimum.X, position.X);
        minimum.Y = std::min(minimum.Y, position.Y);
        minimum.Z = std::min(minimum.Z, position.Z);
        maximum.X = std::max(maximum.X, position.X);
        maximum.Y = std::max(maximum.Y, position.Y);
        maximum.Z = std::max(maximum.Z, position.Z);
    }
    return SelectionBounds::FromCorners(minimum, maximum);
}

void ExtendBounds(
    SelectionBounds& bounds,
    const Position position) noexcept
{
    if (!bounds.Valid)
    {
        bounds = SelectionBounds::FromCorners(position, position);
        return;
    }
    bounds.Minimum.X = std::min(bounds.Minimum.X, position.X);
    bounds.Minimum.Y = std::min(bounds.Minimum.Y, position.Y);
    bounds.Minimum.Z = std::min(bounds.Minimum.Z, position.Z);
    bounds.Maximum.X = std::max(bounds.Maximum.X, position.X);
    bounds.Maximum.Y = std::max(bounds.Maximum.Y, position.Y);
    bounds.Maximum.Z = std::max(bounds.Maximum.Z, position.Z);
}

[[nodiscard]] Position AddSafely(
    const Position position,
    const Position delta,
    bool& representable) noexcept
{
    const auto add = [&representable](
        const std::int32_t value,
        const std::int32_t offset) noexcept
    {
        const std::int64_t result =
            static_cast<std::int64_t>(value) + offset;
        if (result < std::numeric_limits<std::int32_t>::min() ||
            result > std::numeric_limits<std::int32_t>::max())
            representable = false;
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(
            result, std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max()));
    };
    return {
        add(position.X, delta.X),
        add(position.Y, delta.Y),
        add(position.Z, delta.Z)};
}

[[nodiscard]] bool InBounds(
    const Position position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        position.X < static_cast<std::int32_t>(dimensions.X) &&
        position.Y < static_cast<std::int32_t>(dimensions.Y) &&
        position.Z < static_cast<std::int32_t>(dimensions.Z);
}

[[nodiscard]] SelectionBounds TranslateBounds(
    const SelectionBounds& source,
    const Position delta,
    bool& representable) noexcept
{
    if (!source.Valid) return {};
    const Position minimum = AddSafely(
        source.Minimum, delta, representable);
    const Position maximum = AddSafely(
        source.Maximum, delta, representable);
    return representable
        ? SelectionBounds::FromCorners(minimum, maximum)
        : SelectionBounds{};
}

[[nodiscard]] bool BoundsInDimensions(
    const SelectionBounds& bounds,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return bounds.Valid &&
        InBounds(bounds.Minimum, dimensions) &&
        InBounds(bounds.Maximum, dimensions);
}

[[nodiscard]] std::uint64_t NanosecondsSince(
    const std::chrono::steady_clock::time_point begin) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count());
}

[[nodiscard]] std::vector<Position> Combine(
    const std::span<const Position> current,
    const std::span<const Position> projected,
    const SelectionMode mode)
{
    std::vector<Position> result;
    switch (mode)
    {
    case SelectionMode::Replace:
        result.assign(projected.begin(), projected.end());
        break;
    case SelectionMode::Add:
        result.reserve(current.size() + projected.size());
        std::set_union(current.begin(), current.end(),
            projected.begin(), projected.end(), std::back_inserter(result),
            PositionLess);
        break;
    case SelectionMode::Subtract:
        result.reserve(current.size());
        std::set_difference(current.begin(), current.end(),
            projected.begin(), projected.end(), std::back_inserter(result),
            PositionLess);
        break;
    case SelectionMode::Intersect:
        result.reserve(std::min(current.size(), projected.size()));
        std::set_intersection(current.begin(), current.end(),
            projected.begin(), projected.end(), std::back_inserter(result),
            PositionLess);
        break;
    }
    return result;
}
}

std::size_t SelectionMoveInteractionHandler::MoveCacheKeyHash::operator()(
    const MoveCacheKey& key) const noexcept
{
    std::size_t result = std::hash<std::uint64_t>{}(key.SourceIdentity);
    const auto combine = [&result](const std::size_t value) noexcept
    {
        result ^= value + 0x9e3779b9U + (result << 6U) + (result >> 2U);
    };
    combine(std::hash<std::uint64_t>{}(key.DocumentRevision));
    combine(std::hash<std::int32_t>{}(key.Delta.X));
    combine(std::hash<std::int32_t>{}(key.Delta.Y));
    combine(std::hash<std::int32_t>{}(key.Delta.Z));
    return result;
}

ScreenSelectionPlanPtr SelectionMoveInteractionHandler::ResolveSelection(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& currentSelection,
    const ViewportInputFrame& input,
    const ScreenRectangle rectangle,
    const SelectionMode mode,
    const std::uint64_t planId,
    ViewportInteractionMetrics& metrics)
{
    if (!projectionCache_.Ensure(document, input, metrics)) return {};
    const auto projected = projectionCache_.Query(rectangle, metrics);
    auto plan = std::make_shared<ScreenSelectionPlan>();
    plan->PlanId = planId;
    plan->DocumentGeneration = input.DocumentGeneration;
    plan->DocumentRevision = input.DocumentRevision;
    plan->Mode = mode;
    plan->Rectangle = rectangle;
    plan->Voxels = Combine(currentSelection.Voxels(), projected, mode);
    plan->Bounds = BoundsOf(plan->Voxels);
    ++metrics.SelectionRecalculations;
    return plan;
}

SelectionMoveSourceSnapshotPtr
SelectionMoveInteractionHandler::CaptureMoveSource(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    ScreenSelectionPlanPtr selectionPlan,
    ViewportInteractionMetrics& metrics)
{
    const auto dimensions = document.GetDimensions(0U);
    const auto selected = selection.Voxels();
    if (!selectionPlan || !dimensions || selected.empty() ||
        selection.DocumentGeneration() != documentGeneration ||
        selectionPlan->DocumentGeneration != documentGeneration ||
        selectionPlan->DocumentRevision != document.GetRevision() ||
        selectionPlan->Voxels.size() != selected.size() ||
        !std::equal(selectionPlan->Voxels.begin(),
            selectionPlan->Voxels.end(), selected.begin()) ||
        selectionPlan->Bounds != selection.EditableBounds())
        return {};

    auto snapshot = std::make_shared<SelectionMoveSourceSnapshot>();
    snapshot->Identity = selectionPlan->PlanId;
    snapshot->Selection = std::move(selectionPlan);
    snapshot->DocumentGeneration = documentGeneration;
    snapshot->DocumentRevision = document.GetRevision();
    snapshot->Bounds = selection.EditableBounds();
    snapshot->Pivot = snapshot->Bounds.Center();
    snapshot->Dimensions = *dimensions;
    ++metrics.MoveSourceCaptures;
    ++metrics.MoveSourceBoundsBuilds;
    ++metrics.MoveSourcePivotBuilds;
    if (cachedSourceIdentity_ != snapshot->Identity)
    {
        moveCache_.clear();
        cachedSourceIdentity_ = snapshot->Identity;
    }
    return snapshot;
}

SelectionMoveResolveResult SelectionMoveInteractionHandler::ResolveMove(
    const Asset::Voxel::VoxelDocument& document,
    SelectionMoveSourceSnapshotPtr source,
    const Asset::Voxel::VoxelPosition delta,
    const std::uint64_t planId,
    ViewportInteractionMetrics& metrics)
{
    const auto planBegin = std::chrono::steady_clock::now();
    if (source)
    {
        const MoveCacheKey key{
            source->Identity, document.GetRevision(), delta};
        const auto cached = moveCache_.find(key);
        if (cached != moveCache_.end())
        {
            ++metrics.MoveCollisionCacheHits;
            return {cached->second};
        }
        ++metrics.MoveCollisionCacheMisses;
    }

    auto plan = std::make_shared<MovePreviewState>();
    plan->PlanId = planId;
    if (!source)
    {
        plan->Diagnostic = "Move source snapshot is unavailable.";
        return {std::move(plan)};
    }
    plan->DocumentGeneration = source->DocumentGeneration;
    plan->DocumentRevision = document.GetRevision();
    plan->Delta = delta;
    plan->Source = std::move(source);
    plan->SourceBounds = plan->Source->Bounds;
    plan->ExactVoxelCount = plan->Source->Selection->Voxels.size();
    if (plan->Source->DocumentRevision != document.GetRevision())
    {
        plan->Diagnostic = "Move cancelled: model changed.";
        return {std::move(plan)};
    }

    const auto& positions = plan->Source->Selection->Voxels;
    const auto destinationBegin = std::chrono::steady_clock::now();
    bool representable = true;
    plan->DestinationBounds =
        TranslateBounds(plan->SourceBounds, delta, representable);
    metrics.MoveDestinationNanoseconds +=
        NanosecondsSince(destinationBegin);
    ++metrics.MoveCompactUpdates;

    if (!representable ||
        !BoundsInDimensions(
            plan->DestinationBounds, plan->Source->Dimensions))
    {
        plan->OutOfBoundsCount = positions.size();
        plan->OutOfBoundsBounds = plan->DestinationBounds.Valid
            ? plan->DestinationBounds : plan->SourceBounds;
        plan->Validation = MoveValidationState::OutOfBounds;
    }
    else if (positions.size() > InteractiveCollisionVoxelLimit)
    {
        // Large selections stay non-blocking. Exact collision validation is
        // deliberately deferred to the single MouseUp materialization.
        plan->Validation = MoveValidationState::Deferred;
        ++metrics.MoveCollisionDeferred;
    }
    else
    {
        const auto collisionBegin = std::chrono::steady_clock::now();
        std::size_t sourceCursor = 0U;
        for (const Position position : positions)
        {
            bool destinationRepresentable = true;
            const Position destination =
                AddSafely(position, delta, destinationRepresentable);
            ++metrics.MoveSelectedCoordinatesVisited;
            while (sourceCursor < positions.size() &&
                   PositionLess(positions[sourceCursor], destination))
                ++sourceCursor;
            const bool sourceOverlap = sourceCursor < positions.size() &&
                positions[sourceCursor] == destination;
            ++metrics.MoveCollisionQueries;
            if (!sourceOverlap && document.HasVoxel(destination, 0U))
            {
                ++plan->CollisionCount;
                ExtendBounds(plan->CollisionBounds, destination);
            }
        }
        metrics.MoveCollisionNanoseconds +=
            NanosecondsSince(collisionBegin);
        ++metrics.MoveCollisionPasses;
        plan->Validation = plan->CollisionCount == 0U
            ? MoveValidationState::Valid
            : MoveValidationState::Collision;
    }

    const auto previewBegin = std::chrono::steady_clock::now();
    metrics.MovePreviewNanoseconds += NanosecondsSince(previewBegin);
    ++metrics.MovePreviewBuilds;

    if (delta == Position{})
        plan->Diagnostic = "Move delta is zero.";
    else if (plan->Validation == MoveValidationState::OutOfBounds)
        plan->Diagnostic = "Move blocked: destination is outside the model.";
    else if (plan->Validation == MoveValidationState::Collision)
        plan->Diagnostic = "Move blocked: destination is occupied.";
    else if (plan->Validation == MoveValidationState::Deferred)
        plan->Diagnostic =
            "Move collision validation will complete on release.";
    else
        plan->Diagnostic.clear();
    ++metrics.MovePlanBuilds;
    metrics.MovePlanNanoseconds += NanosecondsSince(planBegin);

    const MoveCacheKey cacheKey{
        plan->Source->Identity, plan->DocumentRevision, delta};
    if (moveCache_.size() >= MaximumMoveCacheEntries)
        moveCache_.clear();
    const MovePreviewStatePtr immutable = plan;
    moveCache_.emplace(cacheKey, immutable);
    return {immutable};
}

std::optional<VoxelEditOperation>
SelectionMoveInteractionHandler::BuildCommit(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const MovePreviewState& plan,
    ViewportInteractionMetrics& metrics) const
{
    const auto begin = std::chrono::steady_clock::now();
    ++metrics.MoveCommitBuilds;
    const auto& source = plan.Source;
    if (!plan.CanAttemptCommit() || !source ||
        source->DocumentGeneration != selection.DocumentGeneration() ||
        source->DocumentRevision != document.GetRevision() ||
        source->Selection->Voxels.size() != selection.Count() ||
        !std::equal(source->Selection->Voxels.begin(),
            source->Selection->Voxels.end(), selection.Voxels().begin()))
        return std::nullopt;

    std::vector<Position> destinations;
    destinations.reserve(source->Selection->Voxels.size());
    std::vector<Asset::Voxel::Voxel> sourceValues;
    sourceValues.reserve(source->Selection->Voxels.size());
    std::size_t sourceCursor = 0U;
    for (std::size_t index = 0U;
         index < source->Selection->Voxels.size(); ++index)
    {
        const Position sourcePosition =
            source->Selection->Voxels[index];
        const auto current = document.GetVoxel(sourcePosition, 0U);
        bool representable = true;
        const Position destination =
            AddSafely(sourcePosition, plan.Delta, representable);
        ++metrics.MoveSelectedCoordinatesVisited;
        ++metrics.MoveCommitCoordinatesMaterialized;
        if (!current || !representable ||
            !InBounds(destination, source->Dimensions))
            return std::nullopt;
        while (sourceCursor < source->Selection->Voxels.size() &&
               PositionLess(
                   source->Selection->Voxels[sourceCursor], destination))
            ++sourceCursor;
        const bool sourceOverlap =
            sourceCursor < source->Selection->Voxels.size() &&
            source->Selection->Voxels[sourceCursor] == destination;
        if (!sourceOverlap && document.HasVoxel(destination, 0U))
            return std::nullopt;
        sourceValues.push_back(*current);
        destinations.push_back(destination);
    }

    VoxelEditOperation operation;
    operation.Label = "Move Voxels";
    operation.Changes.reserve(
        source->Selection->Voxels.size() + destinations.size());
    std::size_t sourceIndex = 0U;
    std::size_t destinationIndex = 0U;
    while (sourceIndex < source->Selection->Voxels.size() ||
           destinationIndex < destinations.size())
    {
        Position position{};
        const bool hasSource =
            sourceIndex < source->Selection->Voxels.size();
        const bool hasDestination =
            destinationIndex < destinations.size();
        if (!hasDestination ||
            (hasSource && PositionLess(
                source->Selection->Voxels[sourceIndex],
                destinations[destinationIndex])))
            position = source->Selection->Voxels[sourceIndex];
        else
            position = destinations[destinationIndex];

        const auto before = document.GetVoxel(position, 0U);
        bool afterExists = false;
        Asset::Voxel::Voxel after{};
        if (hasDestination &&
            destinations[destinationIndex] == position)
        {
            afterExists = true;
            after = sourceValues[destinationIndex];
        }
        if (before.has_value() != afterExists ||
            (before && *before != after))
        {
            operation.Changes.push_back({
                0U, position,
                before.has_value(), before ? before->PaletteIndex : 0U,
                afterExists, afterExists ? after.PaletteIndex : 0U});
        }
        if (hasSource &&
            source->Selection->Voxels[sourceIndex] == position)
            ++sourceIndex;
        if (hasDestination &&
            destinations[destinationIndex] == position)
            ++destinationIndex;
    }
    if (operation.Changes.empty()) return std::nullopt;

    auto transition = std::make_shared<VoxelEditSelectionTransition>();
    transition->Before = {
        source->DocumentGeneration, source->Selection->Voxels,
        source->Bounds};
    transition->After = {
        source->DocumentGeneration, std::move(destinations),
        plan.DestinationBounds};
    operation.SelectionTransition = std::move(transition);
    metrics.MoveCommitNanoseconds += NanosecondsSince(begin);
    return operation;
}

SelectionProjectionCache&
SelectionMoveInteractionHandler::ProjectionCache() noexcept
{
    return projectionCache_;
}

std::size_t SelectionMoveInteractionHandler::MoveCacheEntryCount() const
    noexcept
{
    return moveCache_.size();
}

void SelectionMoveInteractionHandler::Reset() noexcept
{
    projectionCache_.Reset();
    ClearMoveCache();
}

void SelectionMoveInteractionHandler::ClearMoveCache() noexcept
{
    moveCache_.clear();
    cachedSourceIdentity_ = 0U;
}

} // namespace VoxelForge::Editor::InteractionV2
