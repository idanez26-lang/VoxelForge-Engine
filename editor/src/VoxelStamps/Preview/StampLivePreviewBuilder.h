#pragma once

#include "Preview/VoxelPreview.h"
#include "VoxelStamps/VoxelStamp.h"

#include <cstddef>

namespace VoxelForge::Editor::Stamps
{

/// Input owned by the caller. The builder reads the optional document only to
/// classify overlaps; no document data is retained in the output preview.
struct StampLivePreviewRequest final
{
    const VoxelStamp* Stamp = nullptr;
    const Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    /// Exact world-space position of the stored Stamp pivot. The caller uses
    /// the same fixed-point transform as the future placement operation.
    StampFixedPoint TargetPivot{};
    bool ForceInvalid = false;
};

/// Stamp adapter for the generic preview engine.  Rotation is deliberately
/// represented by the transform boundary but V1 keeps its neutral orientation.
class StampLivePreviewBuilder final
{
public:
    [[nodiscard]] static VoxelPreviewData Build(
        const StampLivePreviewRequest& request) noexcept;
};

} // namespace VoxelForge::Editor::Stamps
