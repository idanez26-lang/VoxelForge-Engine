#include "SelectionInteraction.h"

#include "EditorMatrix.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VoxelForge::Editor
{

bool SelectionInteraction::ExceedsDragThreshold(
    const float deltaX, const float deltaY) noexcept
{
    return (deltaX * deltaX) + (deltaY * deltaY) >=
        DragThresholdPixels * DragThresholdPixels;
}

bool SelectionInteraction::PointerDown(
    const std::optional<Asset::Voxel::VoxelPosition> target,
    const std::uint64_t documentGeneration,
    const SelectionMode operation,
    const float screenX,
    const float screenY) noexcept
{
    if (IsActive()) return false;
    const bool began = target &&
        BeginCreating(*target, documentGeneration, operation);
    if (began)
    {
        pointerStartX_ = screenX;
        pointerStartY_ = screenY;
    }
    return began;
}

bool SelectionInteraction::PointerMove(
    const float screenX,
    const float screenY,
    const std::optional<Asset::Voxel::VoxelPosition> target,
    const std::optional<Asset::Voxel::VoxelDimensions> dimensions) noexcept
{
    if (!IsActive()) return false;
    if (mode_ == SelectionInteractionMode::ResizingFace)
    {
        if (!dimensions) return false;
        const float axisLengthSquared =
            screenAxisPerVoxel_.X * screenAxisPerVoxel_.X +
            screenAxisPerVoxel_.Y * screenAxisPerVoxel_.Y;
        if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 1.0e-6F)
            return false;
        const float deltaX = screenX - pointerStartX_;
        const float deltaY = screenY - pointerStartY_;
        const float projectedDelta =
            (deltaX * screenAxisPerVoxel_.X +
             deltaY * screenAxisPerVoxel_.Y) / axisLengthSquared;
        if (!std::isfinite(projectedDelta)) return false;
        const std::int32_t gridDelta =
            static_cast<std::int32_t>(std::lround(projectedDelta));
        const SelectionBounds resized = ResizeSelectionBounds(
            originalBounds_, activeFace_, gridDelta, *dimensions);
        if (resized == currentBounds_) return false;
        currentBounds_ = resized;
        return true;
    }
    const bool wasRecognized = dragRecognized_;
    if (!RecognizeDrag(screenX - pointerStartX_, screenY - pointerStartY_))
        return false;
    bool changed = !wasRecognized;
    if (target) changed = Update(*target) || changed;
    if (dimensions) changed = ClampCurrentTo(*dimensions) || changed;
    return changed;
}

bool SelectionInteraction::BeginResizingFace(
    const SelectionFace face,
    const SelectionBounds originalBounds,
    const std::uint64_t documentGeneration,
    const float screenX,
    const float screenY,
    const Vec2 screenAxisPerVoxel) noexcept
{
    const float axisLengthSquared =
        screenAxisPerVoxel.X * screenAxisPerVoxel.X +
        screenAxisPerVoxel.Y * screenAxisPerVoxel.Y;
    if (IsActive() || face == SelectionFace::None || !originalBounds.Valid ||
        documentGeneration == 0U || !std::isfinite(screenX) ||
        !std::isfinite(screenY) || !std::isfinite(axisLengthSquared) ||
        axisLengthSquared <= 1.0e-6F)
        return false;
    mode_ = SelectionInteractionMode::ResizingFace;
    operation_ = SelectionMode::Replace;
    activeFace_ = face;
    originalBounds_ = originalBounds;
    currentBounds_ = originalBounds;
    documentGeneration_ = documentGeneration;
    dragRecognized_ = true;
    pointerStartX_ = screenX;
    pointerStartY_ = screenY;
    screenAxisPerVoxel_ = screenAxisPerVoxel;
    return true;
}

bool SelectionInteraction::BeginMovingBox(
    const SelectionBounds originalBounds,
    const std::uint64_t documentGeneration,
    const SelectionMovePlane movePlane,
    const Vec3 pointerWorldPosition) noexcept
{
    if (IsActive() || !originalBounds.Valid || documentGeneration == 0U ||
        !movePlane.Valid || !IsFinite(movePlane.Point) ||
        !IsFinite(movePlane.Normal) || !IsFinite(pointerWorldPosition))
        return false;
    mode_ = SelectionInteractionMode::MovingBox;
    operation_ = SelectionMode::Replace;
    originalBounds_ = originalBounds;
    currentBounds_ = originalBounds;
    documentGeneration_ = documentGeneration;
    dragRecognized_ = true;
    movePlane_ = movePlane;
    moveAnchorWorld_ = pointerWorldPosition;
    moveDelta_ = {};
    return true;
}

bool SelectionInteraction::MoveBox(
    const Vec3 pointerWorldPosition,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    if (mode_ != SelectionInteractionMode::MovingBox ||
        !IsFinite(pointerWorldPosition))
        return false;
    const Vec3 worldDelta = pointerWorldPosition - moveAnchorWorld_;
    if (!IsFinite(worldDelta)) return false;
    const auto snap = [](const float value) noexcept
    {
        const double bounded = std::clamp(
            static_cast<double>(value),
            static_cast<double>(std::numeric_limits<std::int32_t>::min()),
            static_cast<double>(std::numeric_limits<std::int32_t>::max()));
        return static_cast<std::int32_t>(std::llround(bounded));
    };
    const Asset::Voxel::VoxelPosition requested{
        snap(worldDelta.X), snap(worldDelta.Y), snap(worldDelta.Z)};
    const SelectionBounds translated = TranslateSelectionBounds(
        originalBounds_, requested, dimensions);
    if (translated == currentBounds_) return false;
    currentBounds_ = translated;
    moveDelta_ = {
        currentBounds_.Minimum.X - originalBounds_.Minimum.X,
        currentBounds_.Minimum.Y - originalBounds_.Minimum.Y,
        currentBounds_.Minimum.Z - originalBounds_.Minimum.Z};
    return true;
}

bool SelectionInteraction::BeginMovingContent(
    const SelectionBounds originalBounds,
    const std::uint64_t documentGeneration,
    const SelectionMovePlane movePlane,
    const Vec3 pointerWorldPosition) noexcept
{
    if (!BeginMovingBox(originalBounds, documentGeneration, movePlane,
            pointerWorldPosition))
        return false;
    mode_ = SelectionInteractionMode::MovingContent;
    return true;
}

bool SelectionInteraction::BeginDuplicatingContent(
    const SelectionBounds originalBounds,
    const std::uint64_t documentGeneration,
    const SelectionMovePlane movePlane,
    const Vec3 pointerWorldPosition) noexcept
{
    if (!BeginMovingBox(originalBounds, documentGeneration, movePlane,
            pointerWorldPosition))
        return false;
    mode_ = SelectionInteractionMode::DuplicatingContent;
    return true;
}

bool SelectionInteraction::MoveContent(
    const Vec3 pointerWorldPosition) noexcept
{
    if ((mode_ != SelectionInteractionMode::MovingContent &&
         mode_ != SelectionInteractionMode::DuplicatingContent) ||
        !IsFinite(pointerWorldPosition))
        return false;
    const Vec3 worldDelta = pointerWorldPosition - moveAnchorWorld_;
    if (!IsFinite(worldDelta)) return false;
    const auto snap = [](const float value) noexcept
    {
        const double bounded = std::clamp(
            static_cast<double>(value),
            static_cast<double>(std::numeric_limits<std::int32_t>::min()),
            static_cast<double>(std::numeric_limits<std::int32_t>::max()));
        return static_cast<std::int32_t>(std::llround(bounded));
    };
    const auto add = [](const std::int32_t value,
                        const std::int32_t delta) noexcept
    {
        const std::int64_t result = static_cast<std::int64_t>(value) + delta;
        return static_cast<std::int32_t>(std::clamp(
            result,
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
    };
    const Asset::Voxel::VoxelPosition requested{
        snap(worldDelta.X), snap(worldDelta.Y), snap(worldDelta.Z)};
    SelectionBounds translated = originalBounds_;
    translated.Minimum = {
        add(originalBounds_.Minimum.X, requested.X),
        add(originalBounds_.Minimum.Y, requested.Y),
        add(originalBounds_.Minimum.Z, requested.Z)};
    translated.Maximum = {
        add(originalBounds_.Maximum.X, requested.X),
        add(originalBounds_.Maximum.Y, requested.Y),
        add(originalBounds_.Maximum.Z, requested.Z)};
    if (translated == currentBounds_) return false;
    currentBounds_ = translated;
    moveDelta_ = requested;
    return true;
}

SelectionPointerRelease SelectionInteraction::PointerUp() noexcept
{
    SelectionPointerRelease release;
    if (!IsActive()) return release;
    release.Mode = mode_;
    release.Face = activeFace_;
    release.WasDrag =
        mode_ == SelectionInteractionMode::ResizingFace ||
        mode_ == SelectionInteractionMode::MovingBox ||
        mode_ == SelectionInteractionMode::MovingContent ||
        mode_ == SelectionInteractionMode::DuplicatingContent ||
        dragRecognized_;
    release.Operation = operation_;
    release.Bounds = release.WasDrag ? Commit() : Cancel();
    return release;
}

bool SelectionInteraction::BeginCreating(
    const Asset::Voxel::VoxelPosition anchor,
    const std::uint64_t documentGeneration,
    const SelectionMode mode) noexcept
{
    if (IsActive() || documentGeneration == 0U) return false;
    mode_ = SelectionInteractionMode::Creating;
    operation_ = mode;
    anchor_ = anchor;
    currentBounds_ = SelectionBounds::FromCorners(anchor, anchor);
    originalBounds_ = {};
    activeFace_ = SelectionFace::None;
    documentGeneration_ = documentGeneration;
    return true;
}

bool SelectionInteraction::Update(
    const Asset::Voxel::VoxelPosition target) noexcept
{
    const SelectionBounds previous = currentBounds_;
    if (mode_ != SelectionInteractionMode::Creating) return false;
    currentBounds_ = SelectionBounds::FromCorners(anchor_, target);
    return currentBounds_ != previous;
}

bool SelectionInteraction::RecognizeDrag(
    const float deltaX, const float deltaY) noexcept
{
    if (!IsActive() || dragRecognized_) return dragRecognized_;
    dragRecognized_ = ExceedsDragThreshold(deltaX, deltaY);
    return dragRecognized_;
}

bool SelectionInteraction::ClampCurrentTo(
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    if (!IsActive()) return false;
    const SelectionBounds previous = currentBounds_;
    currentBounds_ = currentBounds_.ClampedTo(dimensions);
    return currentBounds_ != previous;
}

std::optional<SelectionBounds> SelectionInteraction::Commit() noexcept
{
    if (!IsActive()) return std::nullopt;
    const SelectionBounds result = currentBounds_;
    Reset();
    return result;
}

std::optional<SelectionBounds> SelectionInteraction::Cancel() noexcept
{
    if (!IsActive()) return std::nullopt;
    const std::optional<SelectionBounds> restore =
        mode_ == SelectionInteractionMode::ResizingFace ||
        mode_ == SelectionInteractionMode::MovingBox ||
        mode_ == SelectionInteractionMode::MovingContent ||
        mode_ == SelectionInteractionMode::DuplicatingContent
        ? std::optional<SelectionBounds>(originalBounds_)
        : std::nullopt;
    Reset();
    return restore;
}

bool SelectionInteraction::ValidateDocumentGeneration(
    const std::uint64_t documentGeneration) noexcept
{
    if (!IsActive() || documentGeneration_ == documentGeneration) return true;
    Reset();
    return false;
}

bool SelectionInteraction::IsActive() const noexcept
{
    return mode_ != SelectionInteractionMode::Idle;
}
bool SelectionInteraction::IsDragRecognized() const noexcept
{
    return dragRecognized_;
}
SelectionInteractionMode SelectionInteraction::Mode() const noexcept { return mode_; }
const SelectionBounds& SelectionInteraction::CurrentBounds() const noexcept { return currentBounds_; }
const SelectionBounds& SelectionInteraction::OriginalBounds() const noexcept { return originalBounds_; }
SelectionFace SelectionInteraction::ActiveFace() const noexcept { return activeFace_; }
const SelectionMovePlane& SelectionInteraction::MovePlane() const noexcept { return movePlane_; }
Asset::Voxel::VoxelPosition SelectionInteraction::MoveDelta() const noexcept { return moveDelta_; }
std::uint64_t SelectionInteraction::DocumentGeneration() const noexcept { return documentGeneration_; }

void SelectionInteraction::Reset() noexcept
{
    mode_ = SelectionInteractionMode::Idle;
    operation_ = SelectionMode::Replace;
    currentBounds_ = {};
    originalBounds_ = {};
    anchor_ = {};
    activeFace_ = SelectionFace::None;
    screenAxisPerVoxel_ = {};
    movePlane_ = {};
    moveAnchorWorld_ = {};
    moveDelta_ = {};
    documentGeneration_ = 0U;
    dragRecognized_ = false;
    pointerStartX_ = 0.0F;
    pointerStartY_ = 0.0F;
}

} // namespace VoxelForge::Editor
