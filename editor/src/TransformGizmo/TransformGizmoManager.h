#pragma once

#include "TransformGizmoInteraction.h"
#include "TransformGizmoModel.h"
#include "Transform/TransformPivotManager.h"

#include <cstdint>
#include <string_view>

namespace VoxelForge::Editor
{

enum class TransformGizmoCursorRecommendation : std::uint8_t
{
    Default,
    ResizeAll,
    Forbidden
};

enum class TransformGizmoCancellationReason : std::uint8_t
{
    None,
    Explicit,
    ToolChanged,
    SelectionChanged,
    DocumentChanged,
    ViewportUnavailable,
    ContextInvalid,
    Closing
};

struct TransformGizmoRuntimeContext final
{
    bool DocumentOpen = false;
    bool SessionValid = false;
    bool SelectionValid = false;
    bool OperationAvailable = false;
    bool ViewportAvailable = false;
    bool PointerOverViewport = false;
    bool UiCapturesPointer = false;
    bool CameraActive = false;
    bool DragDropActive = false;
    bool Closing = false;
    bool PreviewValid = true;
    ActiveVoxelTool ActiveTool = ActiveVoxelTool::None;
    std::uint64_t DocumentGeneration = 0U;
    SelectionBounds Bounds{};
};

struct TransformGizmoCancellation final
{
    bool Cancelled = false;
    TransformGizmoMode Mode = TransformGizmoMode::None;
    TransformGizmoCancellationReason Reason =
        TransformGizmoCancellationReason::None;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Cancelled;
    }
};

// Coordinates the immutable visual model and the interaction state machine.
// Tool-specific preview and document mutation deliberately remain outside.
class TransformGizmoManager final
{
public:
    TransformGizmoManager(
        TransformGizmoModel& model,
        TransformGizmoInteraction& interaction,
        TransformPivotManager& pivotManager) noexcept;

    [[nodiscard]] bool UpdateView(
        const TransformGizmoUpdateContext& context) noexcept;
    [[nodiscard]] TransformGizmoCancellation UpdateContext(
        const TransformGizmoRuntimeContext& context) noexcept;
    [[nodiscard]] TransformGizmoCancellation OnToolChanged(
        ActiveVoxelTool tool) noexcept;

    [[nodiscard]] TransformGizmoAxis UpdateHover(
        const TransformGizmoPointerInput& input) noexcept;
    [[nodiscard]] bool BeginInteraction(
        const TransformGizmoPointerInput& input) noexcept;
    [[nodiscard]] bool UpdateInteraction(
        const TransformGizmoPointerInput& input) noexcept;
    [[nodiscard]] TransformGizmoDragRelease EndInteraction() noexcept;
    [[nodiscard]] TransformGizmoCancellation CancelInteraction(
        TransformGizmoCancellationReason reason =
            TransformGizmoCancellationReason::Explicit) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool CanBeginInteraction() const noexcept;
    [[nodiscard]] bool IsVisible() const noexcept;
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] TransformGizmoMode Mode() const noexcept;
    [[nodiscard]] TransformGizmoAxis HoveredAxis() const noexcept;
    [[nodiscard]] TransformGizmoAxis ActiveAxis() const noexcept;
    [[nodiscard]] TransformGizmoInteractionState State() const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelPosition Delta() const noexcept;
    [[nodiscard]] std::int32_t QuarterTurns() const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelDimensions TargetDimensions()
        const noexcept;
    [[nodiscard]] std::string_view HelpText() const noexcept;
    [[nodiscard]] TransformGizmoCursorRecommendation CursorRecommendation()
        const noexcept;
    [[nodiscard]] TransformGizmoCancellationReason LastCancellationReason()
        const noexcept;
    [[nodiscard]] const TransformGizmoView& View() const noexcept;

private:
    [[nodiscard]] bool BaseContextValid() const noexcept;
    [[nodiscard]] bool DragContextValid() const noexcept;
    [[nodiscard]] TransformGizmoCancellationReason InvalidReason()
        const noexcept;
    void ClearHover() noexcept;

    TransformGizmoModel& model_;
    TransformGizmoInteraction& interaction_;
    TransformPivotManager& pivotManager_;
    TransformGizmoRuntimeContext context_{};
    TransformGizmoCancellationReason lastCancellationReason_ =
        TransformGizmoCancellationReason::None;
};

} // namespace VoxelForge::Editor
