#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/VoxelStamp.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace VoxelForge::Editor::Stamps
{

enum class StampPlacementSessionResultCode : std::uint8_t
{
    Succeeded,
    Inactive,
    MissingAsset,
    DocumentChanged,
    InvalidPlan
};

[[nodiscard]] constexpr std::string_view StampPlacementSessionResultMessage(
    const StampPlacementSessionResultCode code) noexcept
{
    switch (code)
    {
    case StampPlacementSessionResultCode::Succeeded:
        return "Stamp placement session is active.";
    case StampPlacementSessionResultCode::Inactive:
        return "No Stamp placement session is active.";
    case StampPlacementSessionResultCode::MissingAsset:
        return "The selected Stamp asset is unavailable.";
    case StampPlacementSessionResultCode::DocumentChanged:
        return "The active voxel document changed; Stamp placement was cancelled.";
    case StampPlacementSessionResultCode::InvalidPlan:
        return "The Stamp placement plan is invalid.";
    }
    return "Unknown Stamp placement session status.";
}

enum class StampPlacementSessionState : std::uint8_t
{
    Empty,
    Active,
    Cancelled
};

struct StampPlacementSessionResult final
{
    StampPlacementSessionResultCode Code =
        StampPlacementSessionResultCode::Inactive;
    bool Succeeded = false;
    bool PlanChanged = false;
    bool PreviewChanged = false;
    StampPlacementDiagnosticCode Diagnostic =
        StampPlacementDiagnosticCode::None;
};

enum class StampPlacementSessionPlaceStatus : std::uint8_t
{
    Placed,
    PreviewRefreshed,
    NoChange,
    Inactive,
    DocumentChanged,
    Rejected
};

struct StampPlacementSessionPlaceResult final
{
    StampPlacementSessionPlaceStatus Status =
        StampPlacementSessionPlaceStatus::Inactive;
    bool PreviewChanged = false;
    VoxelEditHistoryResult History{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Status == StampPlacementSessionPlaceStatus::Placed;
    }
};

/// UI-independent owner of one active Stamp placement. It owns the selected
/// Stamp, transform, current immutable plan, preview snapshot, cache key and
/// placement ordinal. No document pointer is retained.
class StampPlacementSession final
{
public:
    [[nodiscard]] StampPlacementSessionResult SelectAsset(
        const VoxelStamp* stamp,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel = 0U,
        StampFixedPoint targetPivot = {},
        StampCollisionPolicy collisionPolicy =
            StampCollisionPolicy::Overwrite);
    [[nodiscard]] StampPlacementSessionResult Begin(
        VoxelStamp stamp,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        std::size_t targetSubModel,
        StampFixedPoint targetPivot,
        StampCollisionPolicy collisionPolicy =
            StampCollisionPolicy::Overwrite);
    [[nodiscard]] StampPlacementSessionResult Rebuild(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetTarget(
        StampFixedPoint targetPivot,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult UpdateTarget(
        StampFixedPoint targetPivot,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult TranslateTarget(
        std::int32_t x,
        std::int32_t y,
        std::int32_t z,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetQuarterRotation(
        std::uint8_t quarterTurns,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult RotateClockwise(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult RotateCounterClockwise(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult SetMirror(
        StampPlacementMirrorMode mirror,
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionResult CycleMirror(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] StampPlacementSessionPlaceResult PlaceOnce(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration,
        VoxelEditSession& editSession,
        VoxelEditHistory& history);
    [[nodiscard]] bool Cancel() noexcept;

    [[nodiscard]] StampPlacementSessionState State() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool IsCurrent(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration) const noexcept;
    [[nodiscard]] const VoxelStamp* ActiveStamp() const noexcept;
    [[nodiscard]] const StampPlacementPlan* CurrentPlan() const noexcept;
    [[nodiscard]] const VoxelPreviewData* CurrentPreview() const noexcept;
    [[nodiscard]] const StampPlacementCacheKey* CacheKey() const noexcept;
    [[nodiscard]] StampFixedPoint Target() const noexcept;
    [[nodiscard]] std::uint8_t QuarterRotation() const noexcept;
    [[nodiscard]] StampPlacementMirrorMode Mirror() const noexcept;
    [[nodiscard]] std::size_t TargetSubModel() const noexcept;
    [[nodiscard]] std::uint64_t PlacementOrdinal() const noexcept;

private:
    [[nodiscard]] StampPlacementSessionResult BuildCurrent(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration);
    [[nodiscard]] bool MatchesDocumentContext(
        const Asset::Voxel::VoxelDocument& document,
        std::uint64_t documentGeneration) const noexcept;

    StampPlacementSessionState state_ = StampPlacementSessionState::Empty;
    std::optional<VoxelStamp> stamp_;
    std::optional<StampPlacementPlan> plan_;
    VoxelPreviewSession preview_;
    StampPlacementTransform transform_{};
    StampCollisionPolicy collisionPolicy_ = StampCollisionPolicy::Overwrite;
    std::size_t targetSubModel_ = 0U;
    std::uint64_t placementOrdinal_ = 0U;
};

} // namespace VoxelForge::Editor::Stamps
