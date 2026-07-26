#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelStamps/Placement/StampPlacementPlan.h"

namespace VoxelForge::Editor::Stamps
{

/// Render-neutral adapter from the immutable placement plan to the common
/// preview contract. It performs no document read, palette mapping,
/// transformation, bounds calculation or collision decision.
class StampLivePreviewBuilder final
{
public:
    [[nodiscard]] static VoxelPreviewData Build(
        const StampPlacementPlan& plan) noexcept;
};

} // namespace VoxelForge::Editor::Stamps
