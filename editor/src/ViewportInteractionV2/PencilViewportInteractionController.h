#pragma once

#include "PencilCompactCommitGateway.h"
#include "PencilInteractionHandler.h"

#include <array>
#include <optional>
#include <span>

namespace VoxelForge::Editor::InteractionV2
{
enum class PencilPreviewValidity : std::uint8_t
{
    Deferred,
    Valid,
    OutOfBounds
};

// Rendering policy only.  Compact means the plan is still exact and valid,
// but a per-voxel ghost expansion would exceed the interactive frame budget.
enum class PencilPreviewDetail : std::uint8_t
{
    None,
    Exact,
    CompactDeferred
};

inline constexpr std::size_t MaximumPencilDetailedPreviewVoxels = 4096U;

// Viewport-facing input only.  The Workspace resolves the real tool, palette,
// brush profile, hit/workplane and document identity before submitting this
// value; this controller owns neither ImGui nor a VoxelDocument session.
struct PencilViewportInputFrame final
{
    std::uint64_t Frame = 0U;
    bool PrimaryPressed = false;
    bool PrimaryHeld = false;
    bool PrimaryReleased = false;
    bool EscapePressed = false;
    bool PencilToolActive = false;
    PencilInteractionInput Interaction{};
    PencilCompactRequest Request{};
    std::optional<Asset::Voxel::VoxelPosition> Target;
};

struct PencilCompactPresentation final
{
    std::uint64_t Revision = 0U;
    PencilPreviewValidity Validity = PencilPreviewValidity::Deferred;
    PencilPreviewDetail Detail = PencilPreviewDetail::None;
    std::span<const PencilCompactPlanPtr> Plans;
    std::size_t ExactVoxelCount = 0U;
    std::optional<SmartBrushBounds> Bounds;
    bool Active = false;
};

struct PencilViewportInteractionMetrics final
{
    std::uint64_t Frames = 0U;
    std::uint64_t PreviewBuilds = 0U;
    std::uint64_t CommitBuilds = 0U;
    std::uint64_t CommitRejected = 0U;
    std::uint64_t RendererUploads = 0U;
    std::uint64_t RendererUploadedBytes = 0U;
    std::uint64_t RendererDrawCalls = 0U;
    std::uint64_t RendererSourceUploads = 0U;
    std::uint64_t RendererDeltaUpdates = 0U;
};

class PencilViewportInteractionController final
{
public:
    void SubmitInput(PencilViewportInputFrame input) noexcept;
    void Tick(const Asset::Voxel::VoxelDocument* document);
    [[nodiscard]] std::optional<VoxelEditOperation> TakeCommit() noexcept;
    void NotifyCommitApplied() noexcept;
    void SetRendererMetrics(std::uint64_t uploads, std::uint64_t bytes,
        std::uint64_t drawCalls, std::uint64_t sourceUploads,
        std::uint64_t deltaUpdates) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool OwnsPointer() const noexcept;
    [[nodiscard]] const PencilCompactPresentation& Presentation() const noexcept;
    [[nodiscard]] const PencilViewportInteractionMetrics& Metrics() const noexcept;
    [[nodiscard]] const PencilInteractionMetrics& PlanningMetrics() const noexcept;

private:
    void RebuildPresentation() noexcept;
    [[nodiscard]] static bool MatchesDocument(
        const PencilCompactRequest& request,
        const Asset::Voxel::VoxelDocument& document) noexcept;
    [[nodiscard]] static std::optional<SmartBrushBounds> BoundsOf(
        std::span<const PencilCompactPlanPtr> plans) noexcept;

    std::optional<PencilViewportInputFrame> pendingInput_;
    PencilViewportInputFrame currentInput_{};
    std::uint64_t processedFrame_ = 0U;
    std::uint64_t nextSessionId_ = 1U;
    std::uint64_t presentationRevision_ = 0U;
    PencilGestureSession session_;
    PencilInteractionHandler handler_;
    SmartToolPlanner planner_;
    std::array<PencilCompactPlanPtr, 1U> hoverPlans_{};
    std::optional<VoxelEditOperation> pendingCommit_;
    PencilCompactPresentation presentation_{};
    PencilViewportInteractionMetrics metrics_{};
};
} // namespace VoxelForge::Editor::InteractionV2
