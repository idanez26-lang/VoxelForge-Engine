#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/VoxelStamp.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace VoxelForge::Editor::Stamps
{

enum class StampPlacementSessionState : std::uint8_t
{
    Empty,
    Active,
    Cancelled
};

struct StampPlacementSessionResult final
{
    bool Succeeded = false;
    bool PlanChanged = false;
    bool PreviewChanged = false;
    StampPlacementDiagnosticCode Diagnostic =
        StampPlacementDiagnosticCode::None;
};

/// UI-independent owner of one active Stamp placement. It owns the selected
/// Stamp, transform, current immutable plan, preview snapshot, cache key and
/// placement ordinal. No document pointer is retained.
class StampPlacementSession final
{
public:
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
    [[nodiscard]] bool Cancel() noexcept;
    void MarkPlacementCommitted() noexcept;

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
