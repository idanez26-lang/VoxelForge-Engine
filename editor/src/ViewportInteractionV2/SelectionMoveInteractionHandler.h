#pragma once

#include "SelectionProjectionCache.h"
#include "SelectionMovePlan.h"

#include "Transform/MoveVoxelSelectionOperation.h"

#include <functional>
#include <optional>
#include <unordered_map>

namespace VoxelForge::Editor::InteractionV2
{

class SelectionMoveInteractionHandler final
{
public:
    [[nodiscard]] ScreenSelectionPlanPtr ResolveSelection(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& currentSelection,
        const ViewportInputFrame& input,
        ScreenRectangle rectangle,
        SelectionMode mode,
        std::uint64_t planId,
        ViewportInteractionMetrics& metrics);

    [[nodiscard]] SelectionMoveSourceSnapshotPtr CaptureMoveSource(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        ScreenSelectionPlanPtr selectionPlan,
        ViewportInteractionMetrics& metrics);

    [[nodiscard]] SelectionMoveResolveResult ResolveMove(
        const Asset::Voxel::VoxelDocument& document,
        SelectionMoveSourceSnapshotPtr source,
        Asset::Voxel::VoxelPosition delta,
        std::uint64_t planId,
        ViewportInteractionMetrics& metrics);

    [[nodiscard]] std::optional<VoxelEditOperation> BuildCommit(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        const MovePreviewState& plan,
        ViewportInteractionMetrics& metrics) const;

    [[nodiscard]] SelectionProjectionCache& ProjectionCache() noexcept;
    // Diagnostic-only inspection point used by deterministic soak coverage.
    // The cache itself remains private and bounded by MaximumMoveCacheEntries.
    [[nodiscard]] std::size_t MoveCacheEntryCount() const noexcept;
    void ClearMoveCache() noexcept;
    void Reset() noexcept;

private:
    struct MoveCacheKey final
    {
        std::uint64_t SourceIdentity = 0U;
        std::uint64_t DocumentRevision = 0U;
        Asset::Voxel::VoxelPosition Delta{};

        [[nodiscard]] bool operator==(
            const MoveCacheKey&) const noexcept = default;
    };

    struct MoveCacheKeyHash final
    {
        [[nodiscard]] std::size_t operator()(
            const MoveCacheKey& key) const noexcept;
    };

    static constexpr std::size_t MaximumMoveCacheEntries = 64U;
    static constexpr std::size_t InteractiveCollisionVoxelLimit = 8'192U;

    SelectionProjectionCache projectionCache_;
    std::unordered_map<MoveCacheKey, MovePreviewStatePtr, MoveCacheKeyHash>
        moveCache_;
    std::uint64_t cachedSourceIdentity_ = 0U;
};

} // namespace VoxelForge::Editor::InteractionV2
