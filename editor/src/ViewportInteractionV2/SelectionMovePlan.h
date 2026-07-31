#pragma once

#include "VoxelHistory/VoxelEditOperation.h"
#include "ViewportPresentation.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor::InteractionV2
{

struct ScreenSelectionPlan final
{
    std::uint64_t PlanId = 0U;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    SelectionMode Mode = SelectionMode::Replace;
    ScreenRectangle Rectangle{};
    std::vector<Asset::Voxel::VoxelPosition> Voxels;
    SelectionBounds Bounds{};
};

using ScreenSelectionPlanPtr = std::shared_ptr<const ScreenSelectionPlan>;

struct SelectionMoveSourceSnapshot final
{
    std::uint64_t Identity = 0U;
    ScreenSelectionPlanPtr Selection;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    SelectionBounds Bounds{};
    SelectionCenter Pivot{};
    Asset::Voxel::VoxelDimensions Dimensions{};
};

using SelectionMoveSourceSnapshotPtr =
    std::shared_ptr<const SelectionMoveSourceSnapshot>;

// Immutable, compact description of one interactive Move intent. It never
// owns per-destination arrays: exact destinations are materialized once, at
// commit, from Source + Delta.
struct MovePreviewState final
{
    std::uint64_t PlanId = 0U;
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    Asset::Voxel::VoxelPosition Delta{};
    SelectionMoveSourceSnapshotPtr Source;
    SelectionBounds SourceBounds{};
    SelectionBounds DestinationBounds{};
    SelectionBounds CollisionBounds{};
    SelectionBounds OutOfBoundsBounds{};
    std::size_t ExactVoxelCount = 0U;
    std::size_t CollisionCount = 0U;
    std::size_t OutOfBoundsCount = 0U;
    MoveValidationState Validation = MoveValidationState::Deferred;
    std::string Diagnostic;

    [[nodiscard]] bool CanAttemptCommit() const noexcept
    {
        return Delta != Asset::Voxel::VoxelPosition{} &&
            Validation != MoveValidationState::Collision &&
            Validation != MoveValidationState::OutOfBounds;
    }
};

using MovePreviewStatePtr = std::shared_ptr<const MovePreviewState>;

struct SelectionMoveResolveResult final
{
    MovePreviewStatePtr Plan;
};

} // namespace VoxelForge::Editor::InteractionV2
