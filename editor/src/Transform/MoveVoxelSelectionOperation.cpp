#include "MoveVoxelSelectionOperation.h"
#include "TransformOperationFramework.h"

#include <utility>

namespace VoxelForge::Editor
{
namespace
{
MoveVoxelSelectionResult Refused(
    const MoveVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
}

MoveVoxelSelectionResult FromCommon(
    TransformOperationBuildResult common)
{
    switch (common.Code)
    {
    case TransformOperationBuildCode::Ready:
        return {MoveVoxelSelectionResultCode::Ready,
            std::move(common.Operation), {}};
    case TransformOperationBuildCode::NoChange:
        return Refused(MoveVoxelSelectionResultCode::NoChange,
            "Move delta is zero.");
    case TransformOperationBuildCode::InvalidPreview:
    case TransformOperationBuildCode::InvalidDestinations:
        return Refused(MoveVoxelSelectionResultCode::InvalidPreview,
            std::move(common.Message));
    case TransformOperationBuildCode::ModelChanged:
        return Refused(MoveVoxelSelectionResultCode::ModelChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::SelectionChanged:
        return Refused(MoveVoxelSelectionResultCode::SelectionChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::Collision:
        return Refused(MoveVoxelSelectionResultCode::Collision,
            std::move(common.Message));
    case TransformOperationBuildCode::OutOfBounds:
        return Refused(MoveVoxelSelectionResultCode::OutOfBounds,
            std::move(common.Message));
    case TransformOperationBuildCode::Failed:
        return Refused(MoveVoxelSelectionResultCode::Failed,
            common.Message.empty()
                ? "Unable to prepare atomic Move."
                : "Unable to prepare atomic Move: " + common.Message);
    }
    return Refused(MoveVoxelSelectionResultCode::Failed,
        "Unable to prepare atomic Move.");
}
}

MoveVoxelSelectionResult MoveVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview)
{
    if (!preview.IsActive())
        return Refused(MoveVoxelSelectionResultCode::InvalidPreview,
            "Move preview is inactive.");
    return FromCommon(TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview,
        {"Move", "Move Voxels",
         {TransformSourcePolicy::RemoveSource,
          TransformCollisionPolicy::MergeOverlap},
         preview.PreviewBounds()}));
}

const char* MoveVoxelSelectionResultCodeName(
    const MoveVoxelSelectionResultCode code) noexcept
{
    switch (code)
    {
    case MoveVoxelSelectionResultCode::Ready: return "Ready";
    case MoveVoxelSelectionResultCode::NoChange: return "No change";
    case MoveVoxelSelectionResultCode::InvalidPreview: return "Invalid preview";
    case MoveVoxelSelectionResultCode::ModelChanged: return "Model changed";
    case MoveVoxelSelectionResultCode::SelectionChanged: return "Selection changed";
    case MoveVoxelSelectionResultCode::Collision: return "Collision";
    case MoveVoxelSelectionResultCode::OutOfBounds: return "Out of bounds";
    case MoveVoxelSelectionResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
