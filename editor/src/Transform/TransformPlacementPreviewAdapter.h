#pragma once

#include "Preview/VoxelPlacementPreview.h"
#include "Transform/TransformPreviewModel.h"

namespace VoxelForge::Editor
{

[[nodiscard]] VoxelPlacementPreview BuildTransformPlacementPreview(
    const TransformPreviewRenderData& preview) noexcept;

} // namespace VoxelForge::Editor
