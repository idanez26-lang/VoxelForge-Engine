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

[[nodiscard]] std::int32_t NormalizeQuarterTurns(
    std::int32_t turns) noexcept
{
    turns %= 4;
    if (turns > 2) turns -= 4;
    if (turns < -2) turns += 4;
    return turns;
}

[[nodiscard]] std::int32_t Coordinate(
    const Position position, const VoxelRotationAxis axis,
    const bool first) noexcept
{
    if (axis == VoxelRotationAxis::X) return first ? position.Y : position.Z;
    if (axis == VoxelRotationAxis::Y) return first ? position.X : position.Z;
    return first ? position.X : position.Y;
}

void SetCoordinate(Position& position, const VoxelRotationAxis axis,
    const bool first, const std::int32_t value) noexcept
{
    if (axis == VoxelRotationAxis::X) (first ? position.Y : position.Z) = value;
    else if (axis == VoxelRotationAxis::Y)
        (first ? position.X : position.Z) = value;
    else (first ? position.X : position.Y) = value;
}

[[nodiscard]] SelectionBounds BoundsFor(
    const std::span<const Position> positions) noexcept
{
    if (positions.empty()) return {};
    Position minimum = positions.front();
    Position maximum = positions.front();
    for (const Position position : positions.subspan(1U))
    {
        minimum.X = std::min(minimum.X, position.X);
        minimum.Y = std::min(minimum.Y, position.Y);
        minimum.Z = std::min(minimum.Z, position.Z);
        maximum.X = std::max(maximum.X, position.X);
        maximum.Y = std::max(maximum.Y, position.Y);
        maximum.Z = std::max(maximum.Z, position.Z);
    }
    return SelectionBounds::FromCorners(minimum, maximum);
}

[[nodiscard]] bool RotateQuarter(
    std::vector<Position>& positions,
    SelectionBounds& bounds,
    const VoxelRotationAxis axis,
    const VoxelRotationDirection direction,
    std::int64_t& correctionFirst,
    std::int64_t& correctionSecond,
    std::string& message)
{
    const std::int64_t minimumFirst = Coordinate(bounds.Minimum, axis, true);
    const std::int64_t maximumFirst = Coordinate(bounds.Maximum, axis, true);
    const std::int64_t minimumSecond = Coordinate(bounds.Minimum, axis, false);
    const std::int64_t maximumSecond = Coordinate(bounds.Maximum, axis, false);
    const std::int64_t centerFirst = minimumFirst + maximumFirst;
    const std::int64_t centerSecond = minimumSecond + maximumSecond;
    const std::int64_t firstExtent = maximumFirst - minimumFirst + 1LL;
    const std::int64_t secondExtent = maximumSecond - minimumSecond + 1LL;
    correctionFirst = (secondExtent & 1LL) - (firstExtent & 1LL);
    correctionSecond = (firstExtent & 1LL) - (secondExtent & 1LL);
    for (Position& position : positions)
    {
        const std::int64_t relativeFirst =
            2LL * Coordinate(position, axis, true) - centerFirst;
        const std::int64_t relativeSecond =
            2LL * Coordinate(position, axis, false) - centerSecond;
        const std::int64_t rotatedFirst =
            direction == VoxelRotationDirection::Clockwise
            ? relativeSecond : -relativeSecond;
        const std::int64_t rotatedSecond =
            direction == VoxelRotationDirection::Clockwise
            ? -relativeFirst : relativeFirst;
        const std::int64_t destinationFirst =
            centerFirst + rotatedFirst + correctionFirst;
        const std::int64_t destinationSecond =
            centerSecond + rotatedSecond + correctionSecond;
        if ((destinationFirst & 1LL) != 0LL ||
            (destinationSecond & 1LL) != 0LL)
        {
            message = "Rotate produced a non-integral voxel destination.";
            return false;
        }
        std::int32_t first = 0;
        std::int32_t second = 0;
        if (!ToInt32(destinationFirst / 2LL, first) ||
            !ToInt32(destinationSecond / 2LL, second))
        {
            message = "Rotate destination is not representable.";
            return false;
        }
        SetCoordinate(position, axis, true, first);
        SetCoordinate(position, axis, false, second);
    }
    bounds = BoundsFor(positions);
    return bounds.Valid;
}
}

VoxelRotationGeometry RotateVoxelSelectionOperation::BuildGeometry(
    const std::span<const Position> sourcePositions,
    const SelectionBounds sourceBounds,
    const VoxelRotationDirection direction)
{
    return BuildGeometry(sourcePositions, sourceBounds,
        VoxelRotationAxis::Y,
        direction == VoxelRotationDirection::Clockwise ? 1 : -1);
}

VoxelRotationGeometry RotateVoxelSelectionOperation::BuildGeometry(
    const std::span<const Position> sourcePositions,
    const SelectionBounds sourceBounds,
    const VoxelRotationAxis axis,
    const std::int32_t quarterTurns)
{
    VoxelRotationGeometry result;
    result.Axis = axis;
    result.QuarterTurns = NormalizeQuarterTurns(quarterTurns);
    result.Direction = result.QuarterTurns < 0
        ? VoxelRotationDirection::CounterClockwise
        : VoxelRotationDirection::Clockwise;
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
    result.Destinations.assign(sourcePositions.begin(), sourcePositions.end());
    result.Bounds = sourceBounds;
    const std::int32_t turns = std::abs(result.QuarterTurns);
    for (std::int32_t turn = 0; turn < turns; ++turn)
    {
        std::int64_t correctionFirst = 0;
        std::int64_t correctionSecond = 0;
        if (!RotateQuarter(result.Destinations, result.Bounds, axis,
                result.Direction, correctionFirst, correctionSecond,
                result.Message))
        {
            result.Destinations.clear();
            return result;
        }
        if (axis == VoxelRotationAxis::Y && turn == 0)
        {
            result.GridCorrection2X = correctionFirst;
            result.GridCorrection2Z = correctionSecond;
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
    result.DestinationCenter2X =
        static_cast<std::int64_t>(result.Bounds.Minimum.X) +
        result.Bounds.Maximum.X;
    result.DestinationCenter2Z =
        static_cast<std::int64_t>(result.Bounds.Minimum.Z) +
        result.Bounds.Maximum.Z;
    return result;
}

RotateVoxelSelectionResult RotateVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    const VoxelRotationDirection direction)
{
    return Build(document, selection, documentGeneration, preview,
        VoxelRotationAxis::Y,
        direction == VoxelRotationDirection::Clockwise ? 1 : -1);
}

RotateVoxelSelectionResult RotateVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    const VoxelRotationAxis axis,
    const std::int32_t quarterTurns)
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
        data.SourcePositions, data.SourceBounds, axis, quarterTurns);
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

const char* VoxelRotationAxisName(const VoxelRotationAxis axis) noexcept
{
    if (axis == VoxelRotationAxis::X) return "X";
    if (axis == VoxelRotationAxis::Y) return "Y";
    return "Z";
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
