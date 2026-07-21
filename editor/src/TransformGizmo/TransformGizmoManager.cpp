#include "TransformGizmoManager.h"

namespace VoxelForge::Editor
{

TransformGizmoManager::TransformGizmoManager(
    TransformGizmoModel& model,
    TransformGizmoInteraction& interaction,
    TransformPivotManager& pivotManager) noexcept
    : model_(model), interaction_(interaction), pivotManager_(pivotManager)
{
}

bool TransformGizmoManager::UpdateView(
    const TransformGizmoUpdateContext& context) noexcept
{
    TransformGizmoUpdateContext resolved = context;
    resolved.PivotValid = pivotManager_.HasValidPivot();
    if (resolved.PivotValid)
        resolved.PivotWorldPosition = pivotManager_.GetPivot().WorldPosition;
    return model_.Update(resolved);
}

TransformGizmoCancellation TransformGizmoManager::UpdateContext(
    const TransformGizmoRuntimeContext& context) noexcept
{
    context_ = context;
    if (interaction_.IsDragging())
    {
        if (!DragContextValid())
            return CancelInteraction(InvalidReason());
        const TransformGizmoMode activeMode = interaction_.Mode();
        if (!interaction_.Validate(
                context_.DocumentGeneration, context_.Bounds, true))
        {
            lastCancellationReason_ =
                TransformGizmoCancellationReason::ContextInvalid;
            return {true, activeMode, lastCancellationReason_};
        }
        return {};
    }
    if (!CanBeginInteraction()) ClearHover();
    return {};
}

TransformGizmoCancellation TransformGizmoManager::OnToolChanged(
    const ActiveVoxelTool tool) noexcept
{
    context_.ActiveTool = tool;
    const TransformGizmoMode nextMode = TransformGizmoModel::ModeForTool(tool);
    if (interaction_.IsDragging() && nextMode != interaction_.Mode())
        return CancelInteraction(TransformGizmoCancellationReason::ToolChanged);
    if (nextMode != model_.View().Mode) ClearHover();
    return {};
}

TransformGizmoAxis TransformGizmoManager::UpdateHover(
    const TransformGizmoPointerInput& input) noexcept
{
    if (!CanBeginInteraction())
    {
        ClearHover();
        return TransformGizmoAxis::None;
    }
    return interaction_.UpdateHover(model_.View(), input);
}

bool TransformGizmoManager::BeginInteraction(
    const TransformGizmoPointerInput& input) noexcept
{
    if (!CanBeginInteraction() ||
        interaction_.HoveredAxis() == TransformGizmoAxis::None)
        return false;
    lastCancellationReason_ = TransformGizmoCancellationReason::None;
    return interaction_.BeginDrag(
        model_.View(), interaction_.HoveredAxis(), input,
        context_.DocumentGeneration, context_.Bounds);
}

bool TransformGizmoManager::UpdateInteraction(
    const TransformGizmoPointerInput& input) noexcept
{
    return DragContextValid() && interaction_.UpdateDrag(input);
}

TransformGizmoDragRelease TransformGizmoManager::EndInteraction() noexcept
{
    return interaction_.EndDrag();
}

TransformGizmoCancellation TransformGizmoManager::CancelInteraction(
    const TransformGizmoCancellationReason reason) noexcept
{
    const TransformGizmoMode previousMode = interaction_.Mode();
    const bool cancelled = interaction_.Cancel();
    if (cancelled) lastCancellationReason_ = reason;
    return {cancelled, previousMode,
        cancelled ? reason : TransformGizmoCancellationReason::None};
}

void TransformGizmoManager::Reset() noexcept
{
    interaction_.Reset();
    model_.Reset();
    context_ = {};
    lastCancellationReason_ = TransformGizmoCancellationReason::None;
}

bool TransformGizmoManager::CanBeginInteraction() const noexcept
{
    const TransformGizmoMode mode =
        TransformGizmoModel::ModeForTool(context_.ActiveTool);
    return BaseContextValid() && context_.ViewportAvailable &&
        context_.PointerOverViewport && !context_.UiCapturesPointer &&
        !context_.CameraActive && !context_.DragDropActive &&
        model_.View().Visible && model_.View().Mode == mode;
}

bool TransformGizmoManager::IsVisible() const noexcept
{
    return model_.View().Visible;
}

bool TransformGizmoManager::IsDragging() const noexcept
{
    return interaction_.IsDragging();
}

TransformGizmoMode TransformGizmoManager::Mode() const noexcept
{
    return interaction_.IsDragging() ? interaction_.Mode() : model_.View().Mode;
}

TransformGizmoAxis TransformGizmoManager::HoveredAxis() const noexcept
{
    return interaction_.HoveredAxis();
}

TransformGizmoAxis TransformGizmoManager::ActiveAxis() const noexcept
{
    return interaction_.IsDragging()
        ? interaction_.LockedAxis() : interaction_.HoveredAxis();
}

TransformGizmoInteractionState TransformGizmoManager::State() const noexcept
{
    return interaction_.State();
}

Asset::Voxel::VoxelPosition TransformGizmoManager::Delta() const noexcept
{
    return interaction_.Delta();
}

std::int32_t TransformGizmoManager::QuarterTurns() const noexcept
{
    return interaction_.QuarterTurns();
}

Asset::Voxel::VoxelDimensions TransformGizmoManager::TargetDimensions()
    const noexcept
{
    return interaction_.TargetDimensions();
}

std::string_view TransformGizmoManager::HelpText() const noexcept
{
    return TransformGizmoModel::ContextHelpFor(
        Mode(), interaction_.State(), ActiveAxis());
}

TransformGizmoCursorRecommendation
TransformGizmoManager::CursorRecommendation() const noexcept
{
    return interaction_.IsDragging() ||
        interaction_.HoveredAxis() != TransformGizmoAxis::None
        ? TransformGizmoCursorRecommendation::ResizeAll
        : TransformGizmoCursorRecommendation::Default;
}

TransformGizmoCancellationReason
TransformGizmoManager::LastCancellationReason() const noexcept
{
    return lastCancellationReason_;
}

const TransformGizmoView& TransformGizmoManager::View() const noexcept
{
    return model_.View();
}

bool TransformGizmoManager::BaseContextValid() const noexcept
{
    const TransformGizmoMode mode =
        TransformGizmoModel::ModeForTool(context_.ActiveTool);
    return context_.DocumentOpen && context_.SessionValid &&
        context_.SelectionValid && context_.OperationAvailable &&
        pivotManager_.HasValidPivot() && !context_.Closing &&
        mode != TransformGizmoMode::None;
}

bool TransformGizmoManager::DragContextValid() const noexcept
{
    return BaseContextValid() && context_.ViewportAvailable &&
        !context_.UiCapturesPointer && !context_.CameraActive &&
        !context_.DragDropActive && context_.PreviewValid &&
        TransformGizmoModel::ModeForTool(context_.ActiveTool) ==
            interaction_.Mode();
}

TransformGizmoCancellationReason TransformGizmoManager::InvalidReason()
    const noexcept
{
    if (context_.Closing) return TransformGizmoCancellationReason::Closing;
    if (!context_.DocumentOpen || !context_.SessionValid)
        return TransformGizmoCancellationReason::DocumentChanged;
    if (!context_.SelectionValid)
        return TransformGizmoCancellationReason::SelectionChanged;
    if (TransformGizmoModel::ModeForTool(context_.ActiveTool) !=
            interaction_.Mode())
        return TransformGizmoCancellationReason::ToolChanged;
    if (!context_.ViewportAvailable)
        return TransformGizmoCancellationReason::ViewportUnavailable;
    return TransformGizmoCancellationReason::ContextInvalid;
}

void TransformGizmoManager::ClearHover() noexcept
{
    if (interaction_.IsDragging()) return;
    const TransformGizmoView hidden;
    const TransformGizmoPointerInput pointer;
    static_cast<void>(interaction_.UpdateHover(hidden, pointer));
}

} // namespace VoxelForge::Editor
