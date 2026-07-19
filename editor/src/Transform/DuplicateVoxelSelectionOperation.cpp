#include "DuplicateVoxelSelectionOperation.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
[[nodiscard]] bool PositionLess(
    const Asset::Voxel::VoxelPosition left,
    const Asset::Voxel::VoxelPosition right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

[[nodiscard]] bool InBounds(
    const Asset::Voxel::VoxelPosition position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        position.X < static_cast<std::int32_t>(dimensions.X) &&
        position.Y < static_cast<std::int32_t>(dimensions.Y) &&
        position.Z < static_cast<std::int32_t>(dimensions.Z);
}

DuplicateVoxelSelectionResult Refused(
    const DuplicateVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
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
    if (preview.DocumentGeneration() != documentGeneration ||
        preview.DocumentRevision() != document.GetRevision())
        return Refused(DuplicateVoxelSelectionResultCode::ModelChanged,
            "Duplicate cancelled: model changed");
    if (selection.DocumentGeneration() != documentGeneration ||
        !preview.IsValidFor(document, selection, documentGeneration))
        return Refused(DuplicateVoxelSelectionResultCode::SelectionChanged,
            "Duplicate cancelled: selection changed");
    if (preview.Delta() == Asset::Voxel::VoxelPosition{})
        return Refused(DuplicateVoxelSelectionResultCode::NoChange,
            "Duplicate delta is zero.");
    if (preview.HasOutOfBounds())
        return Refused(DuplicateVoxelSelectionResultCode::OutOfBounds,
            "Duplicate blocked: destination is outside the model");

    const TransformPreviewOperationData data = preview.OperationData();
    const auto dimensions = document.GetDimensions(data.ModelIndex);
    if (!dimensions || data.Voxels.empty() ||
        data.SourcePositions.size() != data.Voxels.size())
        return Refused(DuplicateVoxelSelectionResultCode::InvalidPreview,
            "Duplicate preview data is incomplete.");

    try
    {
        std::vector<Asset::Voxel::VoxelPosition> destinations;
        destinations.reserve(data.Voxels.size());
        VoxelEditOperation operation;
        operation.Label = "Duplicate Voxels";
        operation.Changes.reserve(data.Voxels.size());
        for (const TransformPreviewVoxel& voxel : data.Voxels)
        {
            const auto source = document.GetVoxel(
                voxel.SourcePosition, data.ModelIndex);
            if (!source || *source != voxel.Value)
                return Refused(DuplicateVoxelSelectionResultCode::ModelChanged,
                    "Duplicate cancelled: model changed");
            if (!InBounds(voxel.PreviewPosition, *dimensions))
                return Refused(DuplicateVoxelSelectionResultCode::OutOfBounds,
                    "Duplicate blocked: destination is outside the model");
            if (document.HasVoxel(voxel.PreviewPosition, data.ModelIndex))
                return Refused(DuplicateVoxelSelectionResultCode::Collision,
                    "Duplicate blocked: destination is occupied");
            destinations.push_back(voxel.PreviewPosition);
            operation.Changes.push_back({
                data.ModelIndex, voxel.PreviewPosition,
                false, 0U, true, voxel.Value.PaletteIndex});
        }
        std::sort(destinations.begin(), destinations.end(), PositionLess);
        if (std::adjacent_find(destinations.begin(), destinations.end()) !=
            destinations.end())
            return Refused(DuplicateVoxelSelectionResultCode::InvalidPreview,
                "Duplicate destinations are not unique.");

        auto transition = std::make_shared<VoxelEditSelectionTransition>();
        transition->Before = {
            documentGeneration,
            std::vector<Asset::Voxel::VoxelPosition>(
                data.SourcePositions.begin(), data.SourcePositions.end()),
            data.SourceBounds};
        transition->After = {
            documentGeneration, std::move(destinations), data.PreviewBounds};
        operation.SelectionTransition = std::move(transition);
        return {DuplicateVoxelSelectionResultCode::Ready,
            std::move(operation), {}};
    }
    catch (const std::exception& exception)
    {
        return Refused(DuplicateVoxelSelectionResultCode::Failed,
            std::string("Unable to prepare atomic Duplicate: ") +
                exception.what());
    }
    catch (...)
    {
        return Refused(DuplicateVoxelSelectionResultCode::Failed,
            "Unable to prepare atomic Duplicate.");
    }
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
