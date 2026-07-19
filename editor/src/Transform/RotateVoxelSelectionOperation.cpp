#include "RotateVoxelSelectionOperation.h"
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

RotateVoxelSelectionResult Refused(
    const RotateVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
}

RotateVoxelSelectionResult FromCommon(
    TransformOperationBuildResult common)
{
    switch (common.Code)
    {
    case TransformOperationBuildCode::Ready:
        return {RotateVoxelSelectionResultCode::Ready,
            std::move(common.Operation), {}};
    case TransformOperationBuildCode::NoChange:
        return Refused(RotateVoxelSelectionResultCode::NoChange,
            "Rotate does not change any voxel.");
    case TransformOperationBuildCode::InvalidDestinations:
        return Refused(RotateVoxelSelectionResultCode::InvalidGeometry,
            std::move(common.Message));
    case TransformOperationBuildCode::InvalidPreview:
        return Refused(RotateVoxelSelectionResultCode::InvalidPreview,
            std::move(common.Message));
    case TransformOperationBuildCode::ModelChanged:
        return Refused(RotateVoxelSelectionResultCode::ModelChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::SelectionChanged:
        return Refused(RotateVoxelSelectionResultCode::SelectionChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::Collision:
        return Refused(RotateVoxelSelectionResultCode::Collision,
            std::move(common.Message));
    case TransformOperationBuildCode::OutOfBounds:
        return Refused(RotateVoxelSelectionResultCode::OutOfBounds,
            std::move(common.Message));
    case TransformOperationBuildCode::Failed:
        return Refused(RotateVoxelSelectionResultCode::Failed,
            common.Message.empty()
                ? "Unable to prepare atomic Rotate."
                : "Unable to prepare atomic Rotate: " + common.Message);
    }
    return Refused(RotateVoxelSelectionResultCode::Failed,
        "Unable to prepare atomic Rotate.");
}
}

VoxelRotationGeometry RotateVoxelSelectionOperation::BuildGeometry(
    const std::span<const Position> sourcePositions,
    const SelectionBounds sourceBounds,
    const VoxelRotationDirection direction)
{
    VoxelRotationGeometry result;
    result.Direction = direction;
    if (sourcePositions.empty() || !sourceBounds.Valid)
    {
        result.Message = "Rotate requires a non-empty selection.";
        return result;
    }

    result.SourceCenter2X =
        static_cast<std::int64_t>(sourceBounds.Minimum.X) +
        sourceBounds.Maximum.X;
    result.SourceCenter2Z =
        static_cast<std::int64_t>(sourceBounds.Minimum.Z) +
        sourceBounds.Maximum.Z;
    result.Pivot2X = result.SourceCenter2X;
    result.Pivot2Z = result.SourceCenter2Z;
    const std::int64_t width =
        static_cast<std::int64_t>(sourceBounds.Maximum.X) -
        sourceBounds.Minimum.X + 1LL;
    const std::int64_t depth =
        static_cast<std::int64_t>(sourceBounds.Maximum.Z) -
        sourceBounds.Minimum.Z + 1LL;
    // Grid convention: rotate around the exact doubled source center. When X
    // and Z extents have different parity, no integral 90-degree destination
    // can retain that center. Select the nearest grid-compatible center using
    // an exact half-cell correction derived only from extent parity. Swapping
    // the extents reverses the correction, so inverse turns and four equal
    // turns return to the original cells without accumulated drift.
    result.GridCorrection2X = (depth & 1LL) - (width & 1LL);
    result.GridCorrection2Z = (width & 1LL) - (depth & 1LL);
    result.Destinations.reserve(sourcePositions.size());
    Position minimum{};
    Position maximum{};
    bool first = true;
    for (const Position source : sourcePositions)
    {
        const std::int64_t relative2X =
            2LL * source.X - result.Pivot2X;
        const std::int64_t relative2Z =
            2LL * source.Z - result.Pivot2Z;
        const std::int64_t rotated2X =
            direction == VoxelRotationDirection::Clockwise
            ? relative2Z : -relative2Z;
        const std::int64_t rotated2Z =
            direction == VoxelRotationDirection::Clockwise
            ? -relative2X : relative2X;
        const std::int64_t destination2X = result.Pivot2X + rotated2X +
            result.GridCorrection2X;
        const std::int64_t destination2Z = result.Pivot2Z + rotated2Z +
            result.GridCorrection2Z;
        if ((destination2X & 1LL) != 0LL ||
            (destination2Z & 1LL) != 0LL)
        {
            result.Message =
                "Rotate produced a non-integral voxel destination.";
            result.Destinations.clear();
            return result;
        }
        Position destination{0, source.Y, 0};
        if (!ToInt32(destination2X / 2LL, destination.X) ||
            !ToInt32(destination2Z / 2LL, destination.Z))
        {
            result.Message = "Rotate destination is not representable.";
            result.Destinations.clear();
            return result;
        }
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
        result.Message = "Rotate destinations are not unique.";
        result.Destinations.clear();
        return result;
    }
    result.Bounds = SelectionBounds::FromCorners(minimum, maximum);
    result.DestinationCenter2X =
        static_cast<std::int64_t>(minimum.X) + maximum.X;
    result.DestinationCenter2Z =
        static_cast<std::int64_t>(minimum.Z) + maximum.Z;
    return result;
}

RotateVoxelSelectionResult RotateVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    const VoxelRotationDirection direction)
{
    if (!preview.IsActive() || !preview.HasExplicitDestinations())
        return Refused(RotateVoxelSelectionResultCode::InvalidPreview,
            "Rotate preview is inactive.");
    TransformOperationBuildResult common = TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview,
        {"Rotate", "Rotate Voxels",
         {TransformSourcePolicy::RemoveSource,
          TransformCollisionPolicy::AllowSourceOverlap},
         preview.PreviewBounds()});
    if (common.Code != TransformOperationBuildCode::Ready &&
        common.Code != TransformOperationBuildCode::NoChange)
        return FromCommon(std::move(common));
    const TransformPreviewOperationData data = preview.OperationData();
    const VoxelRotationGeometry geometry = BuildGeometry(
        data.SourcePositions, data.SourceBounds, direction);
    if (!geometry.Valid() ||
        geometry.Destinations.size() != data.Voxels.size())
        return Refused(RotateVoxelSelectionResultCode::InvalidGeometry,
            geometry.Message.empty()
                ? "Rotate geometry is invalid." : geometry.Message);
    for (std::size_t index = 0U; index < data.Voxels.size(); ++index)
    {
        if (data.Voxels[index].PreviewPosition !=
            geometry.Destinations[index])
            return Refused(RotateVoxelSelectionResultCode::InvalidPreview,
                "Rotate preview does not match its geometry.");
    }
    if (geometry.Bounds != preview.PreviewBounds())
        return Refused(RotateVoxelSelectionResultCode::InvalidGeometry,
            "Rotate destination bounds do not match the preview.");
    return FromCommon(std::move(common));
}

const char* VoxelRotationDirectionName(
    const VoxelRotationDirection direction) noexcept
{
    return direction == VoxelRotationDirection::Clockwise
        ? "Clockwise" : "Counterclockwise";
}

const char* RotateVoxelSelectionResultCodeName(
    const RotateVoxelSelectionResultCode code) noexcept
{
    switch (code)
    {
    case RotateVoxelSelectionResultCode::Ready: return "Ready";
    case RotateVoxelSelectionResultCode::NoChange: return "No change";
    case RotateVoxelSelectionResultCode::InvalidGeometry: return "Invalid geometry";
    case RotateVoxelSelectionResultCode::InvalidPreview: return "Invalid preview";
    case RotateVoxelSelectionResultCode::ModelChanged: return "Model changed";
    case RotateVoxelSelectionResultCode::SelectionChanged: return "Selection changed";
    case RotateVoxelSelectionResultCode::Collision: return "Collision";
    case RotateVoxelSelectionResultCode::OutOfBounds: return "Out of bounds";
    case RotateVoxelSelectionResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
