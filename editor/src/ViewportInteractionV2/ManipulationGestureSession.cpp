#include "ManipulationGestureSession.h"

namespace VoxelForge::Editor::InteractionV2
{

void ManipulationGestureSession::BeginSelection(
    const std::uint64_t sessionId,
    const Vec2 anchor,
    const SelectionMode mode,
    const std::uint64_t documentGeneration) noexcept
{
    phase_ = InteractionPhase::Selecting;
    sessionId_ = sessionId;
    documentGeneration_ = documentGeneration;
    selectionAnchor_ = anchor;
    mode_ = mode;
    selectionPlan_.reset();
    movePlan_.reset();
    moveSource_.reset();
    movePlane_ = {};
    moveAnchorWorld_ = {};
}

void ManipulationGestureSession::SetSelectionPlan(
    ScreenSelectionPlanPtr plan) noexcept
{
    selectionPlan_ = std::move(plan);
}

void ManipulationGestureSession::CompleteSelection() noexcept
{
    phase_ = InteractionPhase::SelectionReady;
    movePlan_.reset();
}

bool ManipulationGestureSession::BeginMove(
    const SelectionMovePlane plane,
    const Vec3 anchorWorld,
    SelectionMoveSourceSnapshotPtr source) noexcept
{
    if (phase_ != InteractionPhase::SelectionReady || !plane.Valid ||
        !source)
        return false;
    phase_ = InteractionPhase::Moving;
    movePlane_ = plane;
    moveAnchorWorld_ = anchorWorld;
    movePlan_.reset();
    moveSource_ = std::move(source);
    return true;
}

void ManipulationGestureSession::SetMovePlan(
    MovePreviewStatePtr plan) noexcept
{
    movePlan_ = std::move(plan);
}

void ManipulationGestureSession::CompleteMove() noexcept
{
    phase_ = InteractionPhase::SelectionReady;
    movePlan_.reset();
    moveSource_.reset();
}

void ManipulationGestureSession::CancelGesture() noexcept
{
    if (phase_ == InteractionPhase::Moving)
    {
        phase_ = InteractionPhase::SelectionReady;
        movePlan_.reset();
        moveSource_.reset();
    }
    else if (phase_ == InteractionPhase::Selecting)
    {
        Reset();
    }
}

void ManipulationGestureSession::Reset() noexcept
{
    phase_ = InteractionPhase::Idle;
    sessionId_ = 0U;
    documentGeneration_ = 0U;
    selectionPlan_.reset();
    movePlan_.reset();
    moveSource_.reset();
    movePlane_ = {};
    moveAnchorWorld_ = {};
}

InteractionPhase ManipulationGestureSession::Phase() const noexcept
{
    return phase_;
}

std::uint64_t ManipulationGestureSession::SessionId() const noexcept
{
    return sessionId_;
}

std::uint64_t ManipulationGestureSession::DocumentGeneration() const noexcept
{
    return documentGeneration_;
}

Vec2 ManipulationGestureSession::SelectionAnchor() const noexcept
{
    return selectionAnchor_;
}

SelectionMode ManipulationGestureSession::Mode() const noexcept
{
    return mode_;
}

const ScreenSelectionPlanPtr&
ManipulationGestureSession::SelectionPlan() const noexcept
{
    return selectionPlan_;
}

const MovePreviewStatePtr&
ManipulationGestureSession::MovePlan() const noexcept
{
    return movePlan_;
}

const SelectionMoveSourceSnapshotPtr&
ManipulationGestureSession::MoveSource() const noexcept
{
    return moveSource_;
}

const SelectionMovePlane&
ManipulationGestureSession::MovePlane() const noexcept
{
    return movePlane_;
}

Vec3 ManipulationGestureSession::MoveAnchorWorld() const noexcept
{
    return moveAnchorWorld_;
}

} // namespace VoxelForge::Editor::InteractionV2
