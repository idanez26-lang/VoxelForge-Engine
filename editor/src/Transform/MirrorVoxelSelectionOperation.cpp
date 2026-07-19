#include "MirrorVoxelSelectionOperation.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;
using Voxel = Asset::Voxel::Voxel;

[[nodiscard]] bool PositionLess(
    const Position left, const Position right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

[[nodiscard]] bool InBounds(
    const Position position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        position.X < static_cast<std::int32_t>(dimensions.X) &&
        position.Y < static_cast<std::int32_t>(dimensions.Y) &&
        position.Z < static_cast<std::int32_t>(dimensions.Z);
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
    if (preview.DocumentGeneration() != documentGeneration ||
        preview.DocumentRevision() != document.GetRevision())
        return Refused(MirrorVoxelSelectionResultCode::ModelChanged,
            "Mirror cancelled: model changed");
    if (selection.DocumentGeneration() != documentGeneration ||
        !preview.IsValidFor(document, selection, documentGeneration))
        return Refused(MirrorVoxelSelectionResultCode::SelectionChanged,
            "Mirror cancelled: model changed");
    if (preview.HasOutOfBounds())
        return Refused(MirrorVoxelSelectionResultCode::OutOfBounds,
            "Mirror blocked: destination is outside the model");
    if (preview.HasCollisions())
        return Refused(MirrorVoxelSelectionResultCode::Collision,
            "Mirror blocked: destination is occupied");

    const TransformPreviewOperationData data = preview.OperationData();
    const auto dimensions = document.GetDimensions(data.ModelIndex);
    if (!dimensions || data.Voxels.empty() ||
        data.SourcePositions.size() != data.Voxels.size())
        return Refused(MirrorVoxelSelectionResultCode::InvalidPreview,
            "Mirror preview data is incomplete.");
    const VoxelMirrorGeometry geometry = BuildGeometry(
        data.SourcePositions, data.SourceBounds, axis);
    if (!geometry.Valid() ||
        geometry.Destinations.size() != data.Voxels.size())
        return Refused(MirrorVoxelSelectionResultCode::InvalidGeometry,
            geometry.Message.empty()
                ? "Mirror geometry is invalid." : geometry.Message);

    try
    {
        std::vector<std::pair<Position, Voxel>> destinations;
        std::vector<Position> affected;
        destinations.reserve(data.Voxels.size());
        affected.reserve(data.Voxels.size() * 2U);
        for (std::size_t index = 0U; index < data.Voxels.size(); ++index)
        {
            const TransformPreviewVoxel& voxel = data.Voxels[index];
            const Position expected = geometry.Destinations[index];
            if (voxel.PreviewPosition != expected)
                return Refused(MirrorVoxelSelectionResultCode::InvalidPreview,
                    "Mirror preview does not match its geometry.");
            const auto source = document.GetVoxel(
                voxel.SourcePosition, data.ModelIndex);
            if (!source || *source != voxel.Value)
                return Refused(MirrorVoxelSelectionResultCode::ModelChanged,
                    "Mirror cancelled: model changed");
            if (!InBounds(expected, *dimensions))
                return Refused(MirrorVoxelSelectionResultCode::OutOfBounds,
                    "Mirror blocked: destination is outside the model");
            const bool internal = std::binary_search(
                data.SourcePositions.begin(), data.SourcePositions.end(),
                expected, PositionLess);
            if (!internal && document.HasVoxel(expected, data.ModelIndex))
                return Refused(MirrorVoxelSelectionResultCode::Collision,
                    "Mirror blocked: destination is occupied");
            destinations.emplace_back(expected, voxel.Value);
            affected.push_back(voxel.SourcePosition);
            affected.push_back(expected);
        }
        std::sort(destinations.begin(), destinations.end(),
            [](const auto& left, const auto& right)
            {
                return PositionLess(left.first, right.first);
            });
        if (std::adjacent_find(destinations.begin(), destinations.end(),
                [](const auto& left, const auto& right)
                {
                    return left.first == right.first;
                }) != destinations.end())
            return Refused(MirrorVoxelSelectionResultCode::InvalidGeometry,
                "Mirror destinations are not unique.");
        std::sort(affected.begin(), affected.end(), PositionLess);
        affected.erase(std::unique(affected.begin(), affected.end()),
            affected.end());

        VoxelEditOperation operation;
        operation.Label = axis == VoxelMirrorAxis::X
            ? "Mirror Voxels X" : "Mirror Voxels Z";
        operation.Changes.reserve(affected.size());
        for (const Position position : affected)
        {
            const auto before = document.GetVoxel(position, data.ModelIndex);
            const auto destination = std::lower_bound(
                destinations.begin(), destinations.end(), position,
                [](const auto& item, const Position candidate)
                {
                    return PositionLess(item.first, candidate);
                });
            const bool hasAfter = destination != destinations.end() &&
                destination->first == position;
            const Voxel after = hasAfter ? destination->second : Voxel{};
            if (before.has_value() == hasAfter &&
                (!before || *before == after))
                continue;
            operation.Changes.push_back({
                data.ModelIndex, position,
                before.has_value(), before ? before->PaletteIndex : 0U,
                hasAfter, hasAfter ? after.PaletteIndex : 0U});
        }
        if (operation.Changes.empty())
            return Refused(MirrorVoxelSelectionResultCode::NoChange,
                "Mirror has no visible effect");

        std::vector<Position> destinationPositions;
        destinationPositions.reserve(destinations.size());
        for (const auto& destination : destinations)
            destinationPositions.push_back(destination.first);
        auto transition = std::make_shared<VoxelEditSelectionTransition>();
        transition->Before = {
            documentGeneration,
            std::vector<Position>(data.SourcePositions.begin(),
                data.SourcePositions.end()),
            data.SourceBounds};
        transition->After = {
            documentGeneration, std::move(destinationPositions),
            geometry.Bounds};
        operation.SelectionTransition = std::move(transition);
        return {MirrorVoxelSelectionResultCode::Ready,
            std::move(operation), {}};
    }
    catch (const std::exception& exception)
    {
        return Refused(MirrorVoxelSelectionResultCode::Failed,
            std::string("Unable to prepare atomic Mirror: ") +
                exception.what());
    }
    catch (...)
    {
        return Refused(MirrorVoxelSelectionResultCode::Failed,
            "Unable to prepare atomic Mirror.");
    }
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
