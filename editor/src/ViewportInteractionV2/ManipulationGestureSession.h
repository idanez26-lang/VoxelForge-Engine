#pragma once

#include "SelectionMovePlan.h"
#include "Selection/SelectionBoxMoveModel.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor::InteractionV2
{

class ManipulationGestureSession final
{
public:
    void BeginSelection(
        std::uint64_t sessionId,
        Vec2 anchor,
        SelectionMode mode,
        std::uint64_t documentGeneration) noexcept;
    void SetSelectionPlan(ScreenSelectionPlanPtr plan) noexcept;
    void CompleteSelection() noexcept;
    [[nodiscard]] bool BeginMove(
        SelectionMovePlane plane,
        Vec3 anchorWorld,
        SelectionMoveSourceSnapshotPtr source) noexcept;
    void SetMovePlan(MovePreviewStatePtr plan) noexcept;
    void CompleteMove() noexcept;
    void CancelGesture() noexcept;
    void Reset() noexcept;

    [[nodiscard]] InteractionPhase Phase() const noexcept;
    [[nodiscard]] std::uint64_t SessionId() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;
    [[nodiscard]] Vec2 SelectionAnchor() const noexcept;
    [[nodiscard]] SelectionMode Mode() const noexcept;
    [[nodiscard]] const ScreenSelectionPlanPtr& SelectionPlan() const noexcept;
    [[nodiscard]] const MovePreviewStatePtr& MovePlan() const noexcept;
    [[nodiscard]] const SelectionMoveSourceSnapshotPtr&
        MoveSource() const noexcept;
    [[nodiscard]] const SelectionMovePlane& MovePlane() const noexcept;
    [[nodiscard]] Vec3 MoveAnchorWorld() const noexcept;

private:
    InteractionPhase phase_ = InteractionPhase::Idle;
    std::uint64_t sessionId_ = 0U;
    std::uint64_t documentGeneration_ = 0U;
    Vec2 selectionAnchor_{};
    SelectionMode mode_ = SelectionMode::Replace;
    ScreenSelectionPlanPtr selectionPlan_;
    MovePreviewStatePtr movePlan_;
    SelectionMoveSourceSnapshotPtr moveSource_;
    SelectionMovePlane movePlane_{};
    Vec3 moveAnchorWorld_{};
};

} // namespace VoxelForge::Editor::InteractionV2
