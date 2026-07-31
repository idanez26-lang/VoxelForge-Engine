#include "ViewportInteractionController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace VoxelForge::Editor::InteractionV2
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

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
}

void ViewportInteractionController::SubmitInput(
    ViewportInputFrame input) noexcept
{
    pendingInput_ = std::move(input);
}

bool ViewportInteractionController::InputAvailable() const noexcept
{
    return currentInput_.ViewportHovered && currentInput_.ViewportFocused &&
        !currentInput_.UiCapturesPointer && !currentInput_.CameraActive &&
        (currentInput_.SelectionToolActive || currentInput_.MoveToolActive);
}

SelectionMode ViewportInteractionController::RequestedSelectionMode()
    const noexcept
{
    if (currentInput_.Control && currentInput_.Shift)
        return SelectionMode::Intersect;
    if (currentInput_.Control) return SelectionMode::Subtract;
    if (currentInput_.Shift) return SelectionMode::Add;
    return SelectionMode::Replace;
}

std::optional<ScreenRectangle>
ViewportInteractionController::ProjectBounds(
    const SelectionBounds& bounds) const noexcept
{
    if (!bounds.Valid) return std::nullopt;
    const float minimumX = static_cast<float>(bounds.Minimum.X) -
        currentInput_.ModelCenter.X;
    const float minimumY = static_cast<float>(bounds.Minimum.Y) -
        currentInput_.ModelCenter.Y;
    const float minimumZ = static_cast<float>(bounds.Minimum.Z) -
        currentInput_.ModelCenter.Z;
    const float maximumX = static_cast<float>(bounds.Maximum.X + 1) -
        currentInput_.ModelCenter.X;
    const float maximumY = static_cast<float>(bounds.Maximum.Y + 1) -
        currentInput_.ModelCenter.Y;
    const float maximumZ = static_cast<float>(bounds.Maximum.Z + 1) -
        currentInput_.ModelCenter.Z;
    float screenMinimumX = currentInput_.Viewport.X +
        currentInput_.Viewport.Width;
    float screenMinimumY = currentInput_.Viewport.Y +
        currentInput_.Viewport.Height;
    float screenMaximumX = currentInput_.Viewport.X;
    float screenMaximumY = currentInput_.Viewport.Y;
    bool projected = false;
    for (const float x : {minimumX, maximumX})
        for (const float y : {minimumY, maximumY})
            for (const float z : {minimumZ, maximumZ})
            {
                const Vec3 ndc = TransformPoint(
                    currentInput_.ViewProjection, {x, y, z});
                if (!IsFinite(ndc)) continue;
                const float screenX = currentInput_.Viewport.X +
                    (ndc.X + 1.0F) * 0.5F *
                        currentInput_.Viewport.Width;
                const float screenY = currentInput_.Viewport.Y +
                    (1.0F - ndc.Y) * 0.5F *
                        currentInput_.Viewport.Height;
                screenMinimumX = std::min(screenMinimumX, screenX);
                screenMinimumY = std::min(screenMinimumY, screenY);
                screenMaximumX = std::max(screenMaximumX, screenX);
                screenMaximumY = std::max(screenMaximumY, screenY);
                projected = true;
            }
    if (!projected) return std::nullopt;
    return ScreenRectangle{
        screenMinimumX, screenMinimumY, screenMaximumX, screenMaximumY};
}

bool ViewportInteractionController::TryBeginMove(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection)
{
    if (!currentInput_.PointerRay || selection.Empty() ||
        !selection.EditableBounds().Valid)
        return false;
    const auto boxHit = PickSelectionBoxInterior(
        selection.EditableBounds(), currentInput_.ModelCenter,
        *currentInput_.PointerRay);
    if (!boxHit) return false;
    const Vec3 anchorWorld = boxHit->WorldPosition;
    const SelectionMovePlane plane = MakeSelectionMovePlane(
        anchorWorld, currentInput_.PointerRay->Direction);
    SelectionMoveSourceSnapshotPtr source = handler_.CaptureMoveSource(
        document, selection, currentInput_.DocumentGeneration,
        session_.SelectionPlan(), metrics_);
    return session_.BeginMove(plane, anchorWorld, std::move(source));
}

bool ViewportInteractionController::ResolveSelection(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection)
{
    const ScreenRectangle rectangle = MakeScreenRectangle(
        session_.SelectionAnchor(), currentInput_.MouseScreen);
    ScreenSelectionPlanPtr plan = handler_.ResolveSelection(
        document, selection, currentInput_, rectangle, session_.Mode(),
        nextPlanId_++, metrics_);
    if (!plan) return false;
    session_.SetSelectionPlan(std::move(plan));
    return true;
}

bool ViewportInteractionController::ResolveMove(
    const Asset::Voxel::VoxelDocument& document)
{
    if (!currentInput_.PointerRay || !session_.MoveSource()) return false;
    const auto pointerWorld = IntersectSelectionMovePlane(
        *currentInput_.PointerRay, session_.MovePlane());
    const auto dimensions = document.GetDimensions(0U);
    if (!pointerWorld || !dimensions) return false;
    const Vec3 difference = *pointerWorld - session_.MoveAnchorWorld();
    Position requested{
        static_cast<std::int32_t>(std::lround(difference.X)),
        static_cast<std::int32_t>(std::lround(difference.Y)),
        static_cast<std::int32_t>(std::lround(difference.Z))};
    const SelectionBounds translated = TranslateSelectionBounds(
        session_.MoveSource()->Bounds, requested, *dimensions);
    if (!translated.Valid) return false;
    const Position delta{
        translated.Minimum.X - session_.MoveSource()->Bounds.Minimum.X,
        translated.Minimum.Y - session_.MoveSource()->Bounds.Minimum.Y,
        translated.Minimum.Z - session_.MoveSource()->Bounds.Minimum.Z};
    if (const auto& current = session_.MovePlan();
        current && current->Delta == delta)
    {
        ++metrics_.MovePlanReuses;
        return false;
    }
    SelectionMoveResolveResult resolved = handler_.ResolveMove(
        document, session_.MoveSource(), delta, nextPlanId_++, metrics_);
    session_.SetMovePlan(std::move(resolved.Plan));
    return true;
}

void ViewportInteractionController::Tick(
    const Asset::Voxel::VoxelDocument* document,
    SelectionService& selection)
{
    if (!pendingInput_ || pendingInput_->Frame == processedFrame_) return;
    currentInput_ = std::move(*pendingInput_);
    pendingInput_.reset();
    processedFrame_ = currentInput_.Frame;
    ++metrics_.Frames;
    std::uint64_t resolvesThisFrame = 0U;
    bool presentationChanged = false;

    if (document == nullptr ||
        (!currentInput_.SelectionToolActive &&
         !currentInput_.MoveToolActive))
    {
        Reset();
        RebuildPresentation();
        return;
    }
    if (session_.Phase() != InteractionPhase::Idle &&
        session_.DocumentGeneration() != currentInput_.DocumentGeneration)
    {
        Reset();
        presentationChanged = true;
    }
    if (currentInput_.EscapePressed)
    {
        session_.CancelGesture();
        handler_.ClearMoveCache();
        RebuildPresentation();
        return;
    }

    if (currentInput_.PrimaryPressed && InputAvailable())
    {
        if (session_.Phase() == InteractionPhase::SelectionReady &&
            TryBeginMove(*document, selection))
        {
            presentationChanged = true;
            if (ResolveMove(*document)) ++resolvesThisFrame;
        }
        else if (currentInput_.SelectionToolActive)
        {
            session_.BeginSelection(
                nextSessionId_++, currentInput_.MouseScreen,
                RequestedSelectionMode(), currentInput_.DocumentGeneration);
            presentationChanged =
                ResolveSelection(*document, selection) ||
                presentationChanged;
            if (presentationChanged) ++resolvesThisFrame;
        }
    }
    else if (session_.Phase() == InteractionPhase::Selecting &&
             (currentInput_.PrimaryHeld ||
              currentInput_.PrimaryReleased))
    {
        if (ResolveSelection(*document, selection))
        {
            presentationChanged = true;
            ++resolvesThisFrame;
        }
    }
    else if (session_.Phase() == InteractionPhase::Moving &&
             (currentInput_.PrimaryHeld ||
              currentInput_.PrimaryReleased))
    {
        if (ResolveMove(*document))
        {
            presentationChanged = true;
            ++resolvesThisFrame;
        }
    }

    if (currentInput_.PrimaryReleased)
    {
        if (session_.Phase() == InteractionPhase::Selecting)
        {
            const auto& plan = session_.SelectionPlan();
            if (plan)
            {
                if (plan->Bounds.Valid)
                    static_cast<void>(selection.ApplySortedVolume(
                        plan->Voxels, plan->Bounds,
                        SelectionMode::Replace));
                else
                    static_cast<void>(selection.Clear());
                ++selectionRevision_;
            }
            session_.CompleteSelection();
            presentationChanged = true;
        }
        else if (session_.Phase() == InteractionPhase::Moving)
        {
            const auto& plan = session_.MovePlan();
            if (plan && plan->CanAttemptCommit())
            {
                pendingCommit_ = handler_.BuildCommit(
                    *document, selection, *plan, metrics_);
                if (!pendingCommit_)
                {
                    session_.CompleteMove();
                    handler_.ClearMoveCache();
                }
            }
            else
            {
                session_.CompleteMove();
                handler_.ClearMoveCache();
            }
            presentationChanged = true;
        }
    }
    metrics_.BusinessResolves += resolvesThisFrame;
    metrics_.MaximumResolvesPerFrame = std::max(
        metrics_.MaximumResolvesPerFrame, resolvesThisFrame);
    if (presentationChanged) RebuildPresentation();
}

std::optional<VoxelEditOperation>
ViewportInteractionController::TakeCommit() noexcept
{
    return std::exchange(pendingCommit_, std::nullopt);
}

void ViewportInteractionController::SetPresentationGpuMetrics(
    const std::uint64_t uploadCount,
    const std::uint64_t bufferRecreationCount,
    const std::uint64_t uploadedBytes,
    const std::uint64_t sourceUploadCount,
    const std::uint64_t sourceUploadedBytes,
    const std::uint64_t deltaUpdateCount) noexcept
{
    metrics_.PresentationUploads = uploadCount;
    metrics_.PresentationBufferRecreations = bufferRecreationCount;
    metrics_.PresentationUploadedBytes = uploadedBytes;
    metrics_.MoveSourceUploads = sourceUploadCount;
    metrics_.MoveSourceUploadedBytes = sourceUploadedBytes;
    metrics_.MoveDeltaGpuUpdates = deltaUpdateCount;
}

void ViewportInteractionController::SynchronizeReadySelection(
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const std::uint64_t documentRevision)
{
    auto plan = std::make_shared<ScreenSelectionPlan>();
    plan->PlanId = nextPlanId_++;
    plan->DocumentGeneration = documentGeneration;
    plan->DocumentRevision = documentRevision;
    plan->Voxels.assign(selection.Voxels().begin(), selection.Voxels().end());
    plan->Bounds = BoundsOf(plan->Voxels);
    session_.SetSelectionPlan(std::move(plan));
    ++selectionRevision_;
}

void ViewportInteractionController::NotifyCommitApplied(
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const std::uint64_t documentRevision)
{
    session_.CompleteMove();
    handler_.ClearMoveCache();
    SynchronizeReadySelection(
        selection, documentGeneration, documentRevision);
    handler_.ProjectionCache().Reset();
    RebuildPresentation();
}

void ViewportInteractionController::RebuildPresentation() noexcept
{
    ViewportPresentation next;
    next.Revision = ++presentationRevision_;
    next.SessionId = session_.SessionId();
    next.SelectionRevision = selectionRevision_;
    next.Phase = session_.Phase();
    const auto& selectionPlan = session_.SelectionPlan();
    if (selectionPlan)
    {
        next.PlanId = selectionPlan->PlanId;
        next.SelectionBox = selectionPlan->Bounds.Valid
            ? std::optional(selectionPlan->Bounds) : std::nullopt;
        next.ExactSelectionCount = selectionPlan->Voxels.size();
        if (selectionPlan->Voxels.size() <=
            TransformPreviewRenderPolicy::IndividualVoxelLimit)
            next.SelectionDetail = selectionPlan->Voxels;
        if (session_.Phase() == InteractionPhase::Selecting)
            next.GestureOverlay2D = selectionPlan->Rectangle;
        else if (session_.Phase() == InteractionPhase::SelectionReady &&
                 selectionPlan->Bounds.Valid)
            next.SelectionScreenBounds =
                ProjectBounds(selectionPlan->Bounds);
    }
    const auto& movePlan = session_.MovePlan();
    if (movePlan)
    {
        next.PlanId = movePlan->PlanId;
        next.MoveValid =
            movePlan->Validation == MoveValidationState::Valid;
        next.SelectionBox = movePlan->DestinationBounds.Valid
            ? std::optional(movePlan->DestinationBounds)
            : next.SelectionBox;
        moveRenderData_ = {
            movePlan->PlanId,
            movePlan->Source ? movePlan->Source->Identity : 0U,
            movePlan->Delta,
            movePlan->Source
                ? std::span<const Position>(
                    movePlan->Source->Selection->Voxels)
                : std::span<const Position>{},
            movePlan->SourceBounds,
            movePlan->DestinationBounds,
            movePlan->CollisionBounds,
            movePlan->OutOfBoundsBounds,
            movePlan->ExactVoxelCount,
            movePlan->CollisionCount,
            movePlan->OutOfBoundsCount,
            movePlan->Validation};
        next.SelectionDetail = {};
        next.MovePreview = &moveRenderData_;
    }
    next.DrawCompactHandles =
        next.Phase == InteractionPhase::SelectionReady &&
        next.SelectionBox.has_value();
    presentation_ = next;
    ++metrics_.PresentationBuilds;
}

void ViewportInteractionController::Reset() noexcept
{
    session_.Reset();
    handler_.Reset();
    pendingCommit_.reset();
    presentation_ = {};
}

const ViewportPresentation&
ViewportInteractionController::Presentation() const noexcept
{
    return presentation_;
}

const ViewportInteractionMetrics&
ViewportInteractionController::Metrics() const noexcept
{
    return metrics_;
}

InteractionPhase ViewportInteractionController::Phase() const noexcept
{
    return session_.Phase();
}

bool ViewportInteractionController::OwnsPointer() const noexcept
{
    return session_.Phase() == InteractionPhase::Selecting ||
        session_.Phase() == InteractionPhase::Moving;
}

bool ViewportInteractionController::Active() const noexcept
{
    return session_.Phase() != InteractionPhase::Idle;
}

} // namespace VoxelForge::Editor::InteractionV2
