#pragma once

#include "Selection/SelectionService.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include <cstdint>
#include <optional>
#include <span>

namespace VoxelForge::Editor::InteractionV2
{

struct ScreenRectangle final
{
    float MinimumX = 0.0F;
    float MinimumY = 0.0F;
    float MaximumX = 0.0F;
    float MaximumY = 0.0F;

    [[nodiscard]] bool Contains(Vec2 point) const noexcept;
    [[nodiscard]] bool Intersects(const ScreenRectangle& other) const noexcept;
    [[nodiscard]] float Width() const noexcept;
    [[nodiscard]] float Height() const noexcept;
    [[nodiscard]] bool operator==(const ScreenRectangle&) const noexcept =
        default;
};

[[nodiscard]] ScreenRectangle MakeScreenRectangle(
    Vec2 first, Vec2 second) noexcept;

enum class InteractionPhase : std::uint8_t
{
    Idle,
    Selecting,
    SelectionReady,
    Moving
};

enum class MoveValidationState : std::uint8_t
{
    Valid,
    Collision,
    OutOfBounds,
    Deferred
};

// Renderer-facing, read-only Move data. SourcePositions is an immutable
// selection handle payload and is consumed only when SourceIdentity changes.
// Delta is the only value expected to change during a drag.
struct MovePreviewPresentation final
{
    std::uint64_t PlanId = 0U;
    std::uint64_t SourceIdentity = 0U;
    Asset::Voxel::VoxelPosition Delta{};
    std::span<const Asset::Voxel::VoxelPosition> SourcePositions;
    SelectionBounds SourceBounds{};
    SelectionBounds DestinationBounds{};
    SelectionBounds CollisionBounds{};
    SelectionBounds OutOfBoundsBounds{};
    std::size_t ExactVoxelCount = 0U;
    std::size_t CollisionCount = 0U;
    std::size_t OutOfBoundsCount = 0U;
    MoveValidationState Validation = MoveValidationState::Deferred;
};

struct ViewportPresentation final
{
    std::uint64_t Revision = 0U;
    std::uint64_t SessionId = 0U;
    std::uint64_t PlanId = 0U;
    std::uint64_t SelectionRevision = 0U;
    InteractionPhase Phase = InteractionPhase::Idle;
    std::optional<ScreenRectangle> GestureOverlay2D;
    std::optional<ScreenRectangle> SelectionScreenBounds;
    std::optional<SelectionBounds> SelectionBox;
    std::span<const Asset::Voxel::VoxelPosition> SelectionDetail;
    const MovePreviewPresentation* MovePreview = nullptr;
    std::size_t ExactSelectionCount = 0U;
    bool DrawCompactHandles = false;
    bool MoveValid = false;
};

struct ViewportInteractionMetrics final
{
    std::uint64_t Frames = 0U;
    std::uint64_t BusinessResolves = 0U;
    std::uint64_t MaximumResolvesPerFrame = 0U;
    std::uint64_t ProjectionBuilds = 0U;
    std::uint64_t ProjectionQueries = 0U;
    std::uint64_t PresentationBuilds = 0U;
    std::uint64_t PresentationUploads = 0U;
    std::uint64_t PresentationBufferRecreations = 0U;
    std::uint64_t SelectionRecalculations = 0U;
    std::uint64_t MovePlanBuilds = 0U;
    std::uint64_t MovePlanReuses = 0U;
    std::uint64_t MoveSourceCaptures = 0U;
    std::uint64_t MoveSourceBoundsBuilds = 0U;
    std::uint64_t MoveSourcePivotBuilds = 0U;
    std::uint64_t MoveDestinationBuilds = 0U;
    std::uint64_t MoveDestinationCoordinatesMaterialized = 0U;
    std::uint64_t MoveSelectedCoordinatesVisited = 0U;
    std::uint64_t MoveCollisionPasses = 0U;
    std::uint64_t MoveCollisionCacheHits = 0U;
    std::uint64_t MoveCollisionCacheMisses = 0U;
    std::uint64_t MoveCollisionQueries = 0U;
    std::uint64_t MoveCollisionDeferred = 0U;
    std::uint64_t MovePreviewBuilds = 0U;
    std::uint64_t MoveCompactUpdates = 0U;
    std::uint64_t MoveCommitBuilds = 0U;
    std::uint64_t MoveCommitCoordinatesMaterialized = 0U;
    std::uint64_t MovePlanNanoseconds = 0U;
    std::uint64_t MoveDestinationNanoseconds = 0U;
    std::uint64_t MoveCollisionNanoseconds = 0U;
    std::uint64_t MovePreviewNanoseconds = 0U;
    std::uint64_t MoveCommitNanoseconds = 0U;
    std::uint64_t CapacityGrowths = 0U;
    std::uint64_t ProjectedVoxelCount = 0U;
    std::uint64_t PresentationUploadedBytes = 0U;
    std::uint64_t MoveSourceUploads = 0U;
    std::uint64_t MoveSourceUploadedBytes = 0U;
    std::uint64_t MoveDeltaGpuUpdates = 0U;
};

} // namespace VoxelForge::Editor::InteractionV2
