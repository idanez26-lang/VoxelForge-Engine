#include "DuplicateVoxelSelectionOperation.h"
#include "TransformOperationFramework.h"

#include <utility>

namespace VoxelForge::Editor
{
namespace
{
DuplicateVoxelSelectionResult Refused(
    const DuplicateVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
}

DuplicateVoxelSelectionResult FromCommon(
    TransformOperationBuildResult common)
{
    switch (common.Code)
    {
    case TransformOperationBuildCode::Ready:
        return {DuplicateVoxelSelectionResultCode::Ready,
            std::move(common.Operation), {}};
    case TransformOperationBuildCode::NoChange:
        return Refused(DuplicateVoxelSelectionResultCode::NoChange,
            "Duplicate delta is zero.");
    case TransformOperationBuildCode::InvalidPreview:
    case TransformOperationBuildCode::InvalidDestinations:
        return Refused(DuplicateVoxelSelectionResultCode::InvalidPreview,
            std::move(common.Message));
    case TransformOperationBuildCode::ModelChanged:
        return Refused(DuplicateVoxelSelectionResultCode::ModelChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::SelectionChanged:
        return Refused(DuplicateVoxelSelectionResultCode::SelectionChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::Collision:
        return Refused(DuplicateVoxelSelectionResultCode::Collision,
            std::move(common.Message));
    case TransformOperationBuildCode::OutOfBounds:
        return Refused(DuplicateVoxelSelectionResultCode::OutOfBounds,
            std::move(common.Message));
    case TransformOperationBuildCode::Failed:
        return Refused(DuplicateVoxelSelectionResultCode::Failed,
            common.Message.empty()
                ? "Unable to prepare atomic Duplicate."
                : "Unable to prepare atomic Duplicate: " + common.Message);
    }
    return Refused(DuplicateVoxelSelectionResultCode::Failed,
        "Unable to prepare atomic Duplicate.");
}
}

DuplicateVoxelSelectionResult DuplicateVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview)
{
    if (!preview.IsActive())
        return Refused(DuplicateVoxelSelectionResultCode::InvalidPreview,
            "Duplicate preview is inactive.");
    return FromCommon(TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview,
        {"Duplicate", "Duplicate Voxels",
         {TransformSourcePolicy::PreserveSource,
          TransformCollisionPolicy::RejectAnyOccupiedDestination},
         preview.PreviewBounds()}));
}

const char* DuplicateVoxelSelectionResultCodeName(
    const DuplicateVoxelSelectionResultCode code) noexcept
{
    switch (code)
    {
    case DuplicateVoxelSelectionResultCode::Ready: return "Ready";
    case DuplicateVoxelSelectionResultCode::NoChange: return "No change";
    case DuplicateVoxelSelectionResultCode::InvalidPreview: return "Invalid preview";
    case DuplicateVoxelSelectionResultCode::ModelChanged: return "Model changed";
    case DuplicateVoxelSelectionResultCode::SelectionChanged: return "Selection changed";
    case DuplicateVoxelSelectionResultCode::Collision: return "Collision";
    case DuplicateVoxelSelectionResultCode::OutOfBounds: return "Out of bounds";
    case DuplicateVoxelSelectionResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
