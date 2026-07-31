#pragma once

#include "ManipulationGestureSession.h"
#include "SelectionMoveInteractionHandler.h"

#include <optional>

namespace VoxelForge::Editor::InteractionV2
{

class ViewportInteractionController final
{
public:
    void SubmitInput(ViewportInputFrame input) noexcept;
    void Tick(
        const Asset::Voxel::VoxelDocument* document,
        SelectionService& selection);
    void SetPresentationGpuMetrics(
        std::uint64_t uploadCount,
        std::uint64_t bufferRecreationCount,
        std::uint64_t uploadedBytes = 0U,
        std::uint64_t sourceUploadCount = 0U,
        std::uint64_t sourceUploadedBytes = 0U,
        std::uint64_t deltaUpdateCount = 0U) noexcept;
    [[nodiscard]] std::optional<VoxelEditOperation> TakeCommit() noexcept;
    void NotifyCommitApplied(
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::uint64_t documentRevision);
    void Reset() noexcept;

    [[nodiscard]] const ViewportPresentation& Presentation() const noexcept;
    [[nodiscard]] const ViewportInteractionMetrics& Metrics() const noexcept;
    [[nodiscard]] InteractionPhase Phase() const noexcept;
    [[nodiscard]] bool OwnsPointer() const noexcept;
    [[nodiscard]] bool Active() const noexcept;

private:
    [[nodiscard]] bool InputAvailable() const noexcept;
    [[nodiscard]] SelectionMode RequestedSelectionMode() const noexcept;
    [[nodiscard]] std::optional<ScreenRectangle> ProjectBounds(
        const SelectionBounds& bounds) const noexcept;
    [[nodiscard]] bool TryBeginMove(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection);
    [[nodiscard]] bool ResolveSelection(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection);
    [[nodiscard]] bool ResolveMove(
        const Asset::Voxel::VoxelDocument& document);
    void RebuildPresentation() noexcept;
    void SynchronizeReadySelection(
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::uint64_t documentRevision);

    std::optional<ViewportInputFrame> pendingInput_;
    ViewportInputFrame currentInput_{};
    std::uint64_t processedFrame_ = 0U;
    std::uint64_t nextSessionId_ = 1U;
    std::uint64_t nextPlanId_ = 1U;
    std::uint64_t selectionRevision_ = 0U;
    std::uint64_t presentationRevision_ = 0U;
    ManipulationGestureSession session_;
    SelectionMoveInteractionHandler handler_;
    std::optional<VoxelEditOperation> pendingCommit_;
    MovePreviewPresentation moveRenderData_{};
    ViewportPresentation presentation_{};
    ViewportInteractionMetrics metrics_{};
};

} // namespace VoxelForge::Editor::InteractionV2
