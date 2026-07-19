#include "MoveVoxelSelectionOperation.h"

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

MoveVoxelSelectionResult Refused(
    const MoveVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
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
    if (preview.DocumentGeneration() != documentGeneration ||
        preview.DocumentRevision() != document.GetRevision())
        return Refused(MoveVoxelSelectionResultCode::ModelChanged,
            "Move cancelled: model changed");
    if (selection.DocumentGeneration() != documentGeneration ||
        !preview.IsValidFor(document, selection, documentGeneration))
        return Refused(MoveVoxelSelectionResultCode::SelectionChanged,
            "Move cancelled: selection changed");
    if (preview.Delta() == Asset::Voxel::VoxelPosition{})
        return Refused(MoveVoxelSelectionResultCode::NoChange,
            "Move delta is zero.");
    if (preview.HasOutOfBounds())
        return Refused(MoveVoxelSelectionResultCode::OutOfBounds,
            "Move blocked: destination is outside the model");
    if (preview.HasCollisions())
        return Refused(MoveVoxelSelectionResultCode::Collision,
            "Move blocked: destination is occupied");

    const TransformPreviewOperationData data = preview.OperationData();
    const auto dimensions = document.GetDimensions(data.ModelIndex);
    if (!dimensions || data.Voxels.empty() ||
        data.SourcePositions.size() != data.Voxels.size())
        return Refused(MoveVoxelSelectionResultCode::InvalidPreview,
            "Move preview data is incomplete.");

    try
    {
        std::vector<Asset::Voxel::VoxelPosition> destinations;
        std::vector<Asset::Voxel::VoxelPosition> affected;
        destinations.reserve(data.Voxels.size());
        affected.reserve(data.Voxels.size() * 2U);
        for (const TransformPreviewVoxel& voxel : data.Voxels)
        {
            const auto source = document.GetVoxel(
                voxel.SourcePosition, data.ModelIndex);
            if (!source || *source != voxel.Value)
                return Refused(MoveVoxelSelectionResultCode::ModelChanged,
                    "Move cancelled: model changed");
            if (!InBounds(voxel.PreviewPosition, *dimensions))
                return Refused(MoveVoxelSelectionResultCode::OutOfBounds,
                    "Move blocked: destination is outside the model");
            const bool internal = std::binary_search(
                data.SourcePositions.begin(), data.SourcePositions.end(),
                voxel.PreviewPosition, PositionLess);
            if (!internal && document.HasVoxel(
                    voxel.PreviewPosition, data.ModelIndex))
                return Refused(MoveVoxelSelectionResultCode::Collision,
                    "Move blocked: destination is occupied");
            destinations.push_back(voxel.PreviewPosition);
            affected.push_back(voxel.SourcePosition);
            affected.push_back(voxel.PreviewPosition);
        }
        std::sort(destinations.begin(), destinations.end(), PositionLess);
        if (std::adjacent_find(destinations.begin(), destinations.end()) !=
            destinations.end())
            return Refused(MoveVoxelSelectionResultCode::InvalidPreview,
                "Move destinations are not unique.");
        std::sort(affected.begin(), affected.end(), PositionLess);
        affected.erase(std::unique(affected.begin(), affected.end()),
            affected.end());

        VoxelEditOperation operation;
        operation.Label = "Move Voxels";
        operation.Changes.reserve(affected.size());
        for (const auto position : affected)
        {
            const auto before = document.GetVoxel(position, data.ModelIndex);
            const auto destination = std::lower_bound(
                data.Voxels.begin(), data.Voxels.end(), position,
                [](const TransformPreviewVoxel& voxel,
                   const Asset::Voxel::VoxelPosition candidate)
                {
                    return PositionLess(voxel.PreviewPosition, candidate);
                });
            const bool hasAfter = destination != data.Voxels.end() &&
                destination->PreviewPosition == position;
            const Asset::Voxel::Voxel after = hasAfter
                ? destination->Value : Asset::Voxel::Voxel{};
            if (before.has_value() == hasAfter &&
                (!before || *before == after))
                continue;
            operation.Changes.push_back({
                data.ModelIndex,
                position,
                before.has_value(),
                before ? before->PaletteIndex : 0U,
                hasAfter,
                hasAfter ? after.PaletteIndex : 0U});
        }
        if (operation.Changes.empty())
            return Refused(MoveVoxelSelectionResultCode::NoChange,
                "Move does not change any voxel.");

        auto transition = std::make_shared<VoxelEditSelectionTransition>();
        transition->Before = {
            documentGeneration,
            std::vector<Asset::Voxel::VoxelPosition>(
                data.SourcePositions.begin(), data.SourcePositions.end()),
            data.SourceBounds};
        transition->After = {
            documentGeneration, std::move(destinations), data.PreviewBounds};
        operation.SelectionTransition = std::move(transition);
        return {MoveVoxelSelectionResultCode::Ready,
            std::move(operation), {}};
    }
    catch (const std::exception& exception)
    {
        return Refused(MoveVoxelSelectionResultCode::Failed,
            std::string("Unable to prepare atomic Move: ") + exception.what());
    }
    catch (...)
    {
        return Refused(MoveVoxelSelectionResultCode::Failed,
            "Unable to prepare atomic Move.");
    }
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
