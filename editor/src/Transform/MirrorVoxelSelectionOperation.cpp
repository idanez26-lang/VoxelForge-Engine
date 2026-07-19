#include "MirrorVoxelSelectionOperation.h"
#include "TransformOperationFramework.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;
[[nodiscard]] bool PositionLess(
    const Position left, const Position right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

[[nodiscard]] bool ToInt32(
    const std::int64_t value, std::int32_t& destination) noexcept
{
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        return false;
    destination = static_cast<std::int32_t>(value);
    return true;
}

MirrorVoxelSelectionResult Refused(
    const MirrorVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
}

MirrorVoxelSelectionResult FromCommon(
    TransformOperationBuildResult common)
{
    switch (common.Code)
    {
    case TransformOperationBuildCode::Ready:
        return {MirrorVoxelSelectionResultCode::Ready,
            std::move(common.Operation), {}};
    case TransformOperationBuildCode::NoChange:
        return Refused(MirrorVoxelSelectionResultCode::NoChange,
            "Mirror has no visible effect");
    case TransformOperationBuildCode::InvalidDestinations:
        return Refused(MirrorVoxelSelectionResultCode::InvalidGeometry,
            std::move(common.Message));
    case TransformOperationBuildCode::InvalidPreview:
        return Refused(MirrorVoxelSelectionResultCode::InvalidPreview,
            std::move(common.Message));
    case TransformOperationBuildCode::ModelChanged:
        return Refused(MirrorVoxelSelectionResultCode::ModelChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::SelectionChanged:
        return Refused(MirrorVoxelSelectionResultCode::SelectionChanged,
            "Mirror cancelled: model changed");
    case TransformOperationBuildCode::Collision:
        return Refused(MirrorVoxelSelectionResultCode::Collision,
            std::move(common.Message));
    case TransformOperationBuildCode::OutOfBounds:
        return Refused(MirrorVoxelSelectionResultCode::OutOfBounds,
            std::move(common.Message));
    case TransformOperationBuildCode::Failed:
        return Refused(MirrorVoxelSelectionResultCode::Failed,
            common.Message.empty()
                ? "Unable to prepare atomic Mirror."
                : "Unable to prepare atomic Mirror: " + common.Message);
    }
    return Refused(MirrorVoxelSelectionResultCode::Failed,
        "Unable to prepare atomic Mirror.");
}
}

VoxelMirrorGeometry MirrorVoxelSelectionOperation::BuildGeometry(
    const std::span<const Position> sourcePositions,
    const SelectionBounds sourceBounds,
    const VoxelMirrorAxis axis)
{
    VoxelMirrorGeometry result;
    result.Axis = axis;
    if (sourcePositions.empty() || !sourceBounds.Valid)
    {
        result.Message = "Mirror requires a non-empty selection.";
        return result;
    }

    const std::int64_t center2X =
        static_cast<std::int64_t>(sourceBounds.Minimum.X) +
        sourceBounds.Maximum.X;
    const std::int64_t center2Z =
        static_cast<std::int64_t>(sourceBounds.Minimum.Z) +
        sourceBounds.Maximum.Z;
    result.Destinations.reserve(sourcePositions.size());
    Position minimum{};
    Position maximum{};
    bool first = true;
    for (const Position source : sourcePositions)
    {
        Position destination = source;
        const std::int64_t mirrored = axis == VoxelMirrorAxis::X
            ? center2X - source.X
            : center2Z - source.Z;
        std::int32_t coordinate = 0;
        if (!ToInt32(mirrored, coordinate))
        {
            result.Message = "Mirror destination is not representable.";
            result.Destinations.clear();
            return result;
        }
        if (axis == VoxelMirrorAxis::X)
            destination.X = coordinate;
        else
            destination.Z = coordinate;
        result.Destinations.push_back(destination);
        if (first)
        {
            minimum = destination;
            maximum = destination;
            first = false;
        }
        else
        {
            minimum.X = std::min(minimum.X, destination.X);
            minimum.Y = std::min(minimum.Y, destination.Y);
            minimum.Z = std::min(minimum.Z, destination.Z);
            maximum.X = std::max(maximum.X, destination.X);
            maximum.Y = std::max(maximum.Y, destination.Y);
            maximum.Z = std::max(maximum.Z, destination.Z);
        }
    }

    std::vector<Position> unique = result.Destinations;
    std::sort(unique.begin(), unique.end(), PositionLess);
    if (std::adjacent_find(unique.begin(), unique.end()) != unique.end())
    {
        result.Message = "Mirror destinations are not unique.";
        result.Destinations.clear();
        return result;
    }
    std::vector<Position> sortedSource(
        sourcePositions.begin(), sourcePositions.end());
    std::sort(sortedSource.begin(), sortedSource.end(), PositionLess);
    result.Bounds = SelectionBounds::FromCorners(minimum, maximum);
    result.Identity = sortedSource == unique;
    return result;
}

MirrorVoxelSelectionResult MirrorVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    const VoxelMirrorAxis axis)
{
    if (!preview.IsActive() || !preview.HasExplicitDestinations())
        return Refused(MirrorVoxelSelectionResultCode::InvalidPreview,
            "Mirror preview is inactive.");
    TransformOperationBuildResult common = TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview,
        {"Mirror",
         axis == VoxelMirrorAxis::X
            ? "Mirror Voxels X" : "Mirror Voxels Z",
         {TransformSourcePolicy::RemoveSource,
          TransformCollisionPolicy::AllowSourceOverlap},
         preview.PreviewBounds()});
    if (common.Code != TransformOperationBuildCode::Ready &&
        common.Code != TransformOperationBuildCode::NoChange)
        return FromCommon(std::move(common));
    const TransformPreviewOperationData data = preview.OperationData();
    const VoxelMirrorGeometry geometry = BuildGeometry(
        data.SourcePositions, data.SourceBounds, axis);
    if (!geometry.Valid() ||
        geometry.Destinations.size() != data.Voxels.size())
        return Refused(MirrorVoxelSelectionResultCode::InvalidGeometry,
            geometry.Message.empty()
                ? "Mirror geometry is invalid." : geometry.Message);
    for (std::size_t index = 0U; index < data.Voxels.size(); ++index)
    {
        if (data.Voxels[index].PreviewPosition !=
            geometry.Destinations[index])
            return Refused(MirrorVoxelSelectionResultCode::InvalidPreview,
                "Mirror preview does not match its geometry.");
    }
    if (geometry.Bounds != preview.PreviewBounds())
        return Refused(MirrorVoxelSelectionResultCode::InvalidGeometry,
            "Mirror destination bounds do not match the preview.");
    return FromCommon(std::move(common));
}

const char* VoxelMirrorAxisName(const VoxelMirrorAxis axis) noexcept
{
    return axis == VoxelMirrorAxis::X ? "X" : "Z";
}

const char* MirrorVoxelSelectionResultCodeName(
    const MirrorVoxelSelectionResultCode code) noexcept
{
    switch (code)
    {
    case MirrorVoxelSelectionResultCode::Ready: return "Ready";
    case MirrorVoxelSelectionResultCode::NoChange: return "No change";
    case MirrorVoxelSelectionResultCode::InvalidGeometry: return "Invalid geometry";
    case MirrorVoxelSelectionResultCode::InvalidPreview: return "Invalid preview";
    case MirrorVoxelSelectionResultCode::ModelChanged: return "Model changed";
    case MirrorVoxelSelectionResultCode::SelectionChanged: return "Selection changed";
    case MirrorVoxelSelectionResultCode::Collision: return "Collision";
    case MirrorVoxelSelectionResultCode::OutOfBounds: return "Out of bounds";
    case MirrorVoxelSelectionResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
