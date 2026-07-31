#include "ViewportInteractionV2/PencilViewportInteractionController.h"

#include <algorithm>
#include <utility>

namespace VoxelForge::Editor::InteractionV2
{
void PencilViewportInteractionController::SubmitInput(
    PencilViewportInputFrame input) noexcept
{
    pendingInput_ = std::move(input);
}

std::optional<SmartBrushBounds> PencilViewportInteractionController::BoundsOf(
    const std::span<const PencilCompactPlanPtr> plans) noexcept
{
    std::optional<SmartBrushBounds> result;
    for (const PencilCompactPlanPtr& plan : plans)
    {
        if (!plan) continue;
        if (!result) result = plan->Bounds();
        else
        {
            result->Minimum.X = std::min(result->Minimum.X, plan->Bounds().Minimum.X);
            result->Minimum.Y = std::min(result->Minimum.Y, plan->Bounds().Minimum.Y);
            result->Minimum.Z = std::min(result->Minimum.Z, plan->Bounds().Minimum.Z);
            result->Maximum.X = std::max(result->Maximum.X, plan->Bounds().Maximum.X);
            result->Maximum.Y = std::max(result->Maximum.Y, plan->Bounds().Maximum.Y);
            result->Maximum.Z = std::max(result->Maximum.Z, plan->Bounds().Maximum.Z);
        }
    }
    return result;
}

bool PencilViewportInteractionController::MatchesDocument(
    const PencilCompactRequest& request,
    const Asset::Voxel::VoxelDocument& document) noexcept
{
    const std::optional<Asset::Voxel::VoxelDimensions> dimensions =
        document.GetDimensions(0U);
    return dimensions && request.Dimensions.X == dimensions->X &&
        request.Dimensions.Y == dimensions->Y &&
        request.Dimensions.Z == dimensions->Z &&
        request.DocumentRevision == document.GetRevision();
}

void PencilViewportInteractionController::RebuildPresentation() noexcept
{
    PencilCompactPresentation next;
    next.Revision = ++presentationRevision_;
    next.Active = session_.OwnsPointer();
    next.Plans = session_.OwnsPointer()
        ? std::span<const PencilCompactPlanPtr>(session_.Plans())
        : std::span<const PencilCompactPlanPtr>(hoverPlans_);
    next.Bounds = BoundsOf(next.Plans);
    for (const PencilCompactPlanPtr& plan : next.Plans)
    {
        if (!plan) continue;
        next.ExactVoxelCount += plan->ExactVoxelCount();
        if (!plan->IsInsideDocument())
            next.Validity = PencilPreviewValidity::OutOfBounds;
        else if (next.Validity != PencilPreviewValidity::OutOfBounds)
            next.Validity = PencilPreviewValidity::Valid;
    }
    if (next.Plans.empty() || !next.Plans.front())
    {
        next.Plans = {};
        next.Validity = PencilPreviewValidity::Deferred;
    }
    else
    {
        next.Detail = next.ExactVoxelCount <=
                MaximumPencilDetailedPreviewVoxels
            ? PencilPreviewDetail::Exact
            : PencilPreviewDetail::CompactDeferred;
        if (next.Detail == PencilPreviewDetail::CompactDeferred)
            next.Validity = PencilPreviewValidity::Deferred;
    }
    presentation_ = next;
    ++metrics_.PreviewBuilds;
}

void PencilViewportInteractionController::Tick(
    const Asset::Voxel::VoxelDocument* const document)
{
    if (!pendingInput_ || pendingInput_->Frame == processedFrame_) return;
    currentInput_ = std::move(*pendingInput_);
    pendingInput_.reset();
    processedFrame_ = currentInput_.Frame;
    ++metrics_.Frames;

    if (document == nullptr || !currentInput_.PencilToolActive)
    {
        Reset();
        return;
    }
    if (currentInput_.Target && !MatchesDocument(currentInput_.Request, *document))
    {
        // A hover plan is source-bound.  Never present a footprint computed
        // for a stale revision or another document shape.
        Reset();
        return;
    }
    if (session_.OwnsPointer() && currentInput_.Target &&
        (session_.DocumentGeneration() != currentInput_.Request.DocumentGeneration ||
         session_.DocumentRevision() != currentInput_.Request.DocumentRevision))
    {
        session_.Cancel();
        RebuildPresentation();
        return;
    }
    if (currentInput_.EscapePressed || currentInput_.Interaction.FocusLost)
    {
        handler_.Escape(session_);
        RebuildPresentation();
        return;
    }

    bool changed = false;
    if (!session_.OwnsPointer() && !currentInput_.PrimaryPressed)
    {
        PencilCompactPlanPtr nextHover;
        if (currentInput_.Target &&
            handler_.CanAccept(currentInput_.Interaction, session_))
        {
            nextHover = handler_.PlanHover(planner_, currentInput_.Request,
                *currentInput_.Target);
        }
        if (hoverPlans_.front() != nextHover)
        {
            hoverPlans_.front() = std::move(nextHover);
            changed = true;
        }
    }
    if (currentInput_.PrimaryPressed && currentInput_.Target &&
        handler_.Begin(session_, nextSessionId_++, currentInput_.Request,
            currentInput_.Interaction))
    {
        hoverPlans_.front().reset();
        changed = handler_.Drag(session_, planner_, *currentInput_.Target,
            currentInput_.Request.Placement.Normal, currentInput_.Interaction);
    }
    else if (session_.OwnsPointer() && currentInput_.PrimaryHeld &&
             currentInput_.Target)
    {
        changed = handler_.Drag(session_, planner_, *currentInput_.Target,
            currentInput_.Request.Placement.Normal, currentInput_.Interaction);
    }
    else if (session_.OwnsPointer() && currentInput_.PrimaryHeld)
    {
        // A missing/invalid hit suspends this segment instead of connecting a
        // later valid target through empty viewport space.
        changed = session_.Suspend();
    }

    if (currentInput_.PrimaryReleased && session_.OwnsPointer())
    {
        if (currentInput_.Target)
            changed = handler_.Drag(session_, planner_, *currentInput_.Target,
                currentInput_.Request.Placement.Normal,
                currentInput_.Interaction) || changed;
        ++metrics_.CommitBuilds;
        pendingCommit_ = PencilCompactCommitGateway::Build(*document,
            currentInput_.Request.DocumentGeneration, session_.Plans());
        if (!pendingCommit_) ++metrics_.CommitRejected;
        session_.Complete();
        changed = true;
    }
    // A held pointer over the same quantized target changes neither plan nor
    // presentation.  Rebuilding here would create a new preview revision and
    // force downstream render work every frame during a stationary stroke.
    if (changed) RebuildPresentation();
}

std::optional<VoxelEditOperation> PencilViewportInteractionController::TakeCommit() noexcept
{
    return std::exchange(pendingCommit_, std::nullopt);
}

void PencilViewportInteractionController::NotifyCommitApplied() noexcept
{
    session_.Reset();
    hoverPlans_.front().reset();
    RebuildPresentation();
}

void PencilViewportInteractionController::SetRendererMetrics(
    const std::uint64_t uploads, const std::uint64_t bytes,
    const std::uint64_t drawCalls, const std::uint64_t sourceUploads,
    const std::uint64_t deltaUpdates) noexcept
{
    metrics_.RendererUploads = uploads;
    metrics_.RendererUploadedBytes = bytes;
    metrics_.RendererDrawCalls = drawCalls;
    metrics_.RendererSourceUploads = sourceUploads;
    metrics_.RendererDeltaUpdates = deltaUpdates;
}

void PencilViewportInteractionController::Reset() noexcept
{
    session_.Reset();
    hoverPlans_.front().reset();
    pendingCommit_.reset();
    presentation_ = {};
}

bool PencilViewportInteractionController::OwnsPointer() const noexcept
{
    return session_.OwnsPointer();
}

const PencilCompactPresentation&
PencilViewportInteractionController::Presentation() const noexcept
{
    return presentation_;
}

const PencilViewportInteractionMetrics&
PencilViewportInteractionController::Metrics() const noexcept
{
    return metrics_;
}

const PencilInteractionMetrics&
PencilViewportInteractionController::PlanningMetrics() const noexcept
{
    return handler_.Metrics();
}
} // namespace VoxelForge::Editor::InteractionV2
