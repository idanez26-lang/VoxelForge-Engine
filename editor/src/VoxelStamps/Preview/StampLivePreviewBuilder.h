#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelStamps/Placement/StampPlacementPlan.h"

namespace VoxelForge::Editor::Stamps
{

/// Render-neutral adapter from the immutable placement plan to the common
/// preview contract. It performs no document read, palette mapping,
/// transformation, bounds calculation or collision decision.
[[nodiscard]] VoxelPreviewData BuildStampPreview(
    const StampPlacementPlan& plan) noexcept;

/// Backward-compatible facade retained for the existing placement session and
/// smoke-test call sites. New domain code should use BuildStampPreview.
class StampLivePreviewBuilder final
{
public:
    [[nodiscard]] static VoxelPreviewData Build(
        const StampPlacementPlan& plan) noexcept;
};

} // namespace VoxelForge::Editor::Stamps
