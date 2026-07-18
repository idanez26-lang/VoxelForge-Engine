#include "SelectionInteraction.h"

#include <cmath>

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
    const bool wasRecognized = dragRecognized_;
    if (!RecognizeDrag(screenX - pointerStartX_, screenY - pointerStartY_))
        return false;
    bool changed = !wasRecognized;
    if (target) changed = Update(*target) || changed;
    if (dimensions) changed = ClampCurrentTo(*dimensions) || changed;
    return changed;
}

SelectionPointerRelease SelectionInteraction::PointerUp() noexcept
{
    SelectionPointerRelease release;
    if (!IsActive()) return release;
    release.WasDrag = dragRecognized_;
    release.Operation = operation_;
    release.Bounds = dragRecognized_ ? Commit() : Cancel();
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
    Reset();
    return std::nullopt;
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
std::uint64_t SelectionInteraction::DocumentGeneration() const noexcept { return documentGeneration_; }

void SelectionInteraction::Reset() noexcept
{
    mode_ = SelectionInteractionMode::Idle;
    operation_ = SelectionMode::Replace;
    currentBounds_ = {};
    anchor_ = {};
    documentGeneration_ = 0U;
    dragRecognized_ = false;
    pointerStartX_ = 0.0F;
    pointerStartY_ = 0.0F;
}

} // namespace VoxelForge::Editor
