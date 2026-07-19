#include "ScaleVoxelSelectionOperation.h"
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

[[nodiscard]] bool ScaleCoordinate(
    const std::int32_t source,
    const std::int32_t minimum,
    std::int32_t& destination) noexcept
{
    const std::int64_t local =
        static_cast<std::int64_t>(source) - minimum;
    const std::int64_t scaled =
        static_cast<std::int64_t>(minimum) + local * 2;
    if (local < 0 || scaled < std::numeric_limits<std::int32_t>::min() ||
        scaled > std::numeric_limits<std::int32_t>::max())
        return false;
    destination = static_cast<std::int32_t>(scaled);
    return true;
}

[[nodiscard]] bool Increment(
    const std::int32_t value,
    const std::int32_t offset,
    std::int32_t& destination) noexcept
{
    const std::int64_t result =
        static_cast<std::int64_t>(value) + offset;
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max())
        return false;
    destination = static_cast<std::int32_t>(result);
    return true;
}

ScaleVoxelSelectionResult Refused(
    const ScaleVoxelSelectionResultCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
}

ScaleVoxelSelectionResult FromCommon(
    TransformOperationBuildResult common)
{
    switch (common.Code)
    {
    case TransformOperationBuildCode::Ready:
        return {ScaleVoxelSelectionResultCode::Ready,
            std::move(common.Operation), {}};
    case TransformOperationBuildCode::NoChange:
        return Refused(ScaleVoxelSelectionResultCode::InvalidGeometry,
            "Scale produced no voxel changes.");
    case TransformOperationBuildCode::InvalidDestinations:
        return Refused(ScaleVoxelSelectionResultCode::InvalidGeometry,
            std::move(common.Message));
    case TransformOperationBuildCode::InvalidPreview:
        return Refused(ScaleVoxelSelectionResultCode::InvalidPreview,
            std::move(common.Message));
    case TransformOperationBuildCode::ModelChanged:
        return Refused(ScaleVoxelSelectionResultCode::ModelChanged,
            std::move(common.Message));
    case TransformOperationBuildCode::SelectionChanged:
        return Refused(ScaleVoxelSelectionResultCode::SelectionChanged,
            "Scale cancelled: model changed");
    case TransformOperationBuildCode::Collision:
        return Refused(ScaleVoxelSelectionResultCode::Collision,
            std::move(common.Message));
    case TransformOperationBuildCode::OutOfBounds:
        return Refused(ScaleVoxelSelectionResultCode::OutOfBounds,
            std::move(common.Message));
    case TransformOperationBuildCode::Failed:
        return Refused(ScaleVoxelSelectionResultCode::Failed,
            common.Message.empty()
                ? "Unable to prepare atomic Scale."
                : "Unable to prepare atomic Scale: " + common.Message);
    }
    return Refused(ScaleVoxelSelectionResultCode::Failed,
        "Unable to prepare atomic Scale.");
}

[[nodiscard]] std::size_t Multiplier(const VoxelScaleMode mode) noexcept
{
    return mode == VoxelScaleMode::Uniform
        ? ScaleVoxelSelectionOperation::UniformFactor
        : ScaleVoxelSelectionOperation::AxisFactor;
}
}

VoxelScaleGeometry ScaleVoxelSelectionOperation::BuildGeometry(
    const std::span<const TransformPreviewVoxel> sourceVoxels,
    const SelectionBounds sourceBounds,
    const VoxelScaleMode mode)
{
    VoxelScaleGeometry result;
    result.Mode = mode;
    result.SourceCount = sourceVoxels.size();
    if (sourceVoxels.empty() || !sourceBounds.Valid)
    {
        result.Message = "Scale requires a non-empty selection.";
        return result;
    }
    const std::size_t multiplier = Multiplier(mode);
    if (sourceVoxels.size() >
        std::numeric_limits<std::size_t>::max() / multiplier)
    {
        result.Message = "Scale destination count is not representable.";
        return result;
    }
    const std::size_t destinationCount = sourceVoxels.size() * multiplier;
    if (destinationCount > result.Destinations.max_size())
    {
        result.Message = "Scale destination count exceeds container limits.";
        return result;
    }

    result.Destinations.reserve(destinationCount);
    Position minimum{};
    Position maximum{};
    bool first = true;
    for (const TransformPreviewVoxel& sourceVoxel : sourceVoxels)
    {
        const Position source = sourceVoxel.SourcePosition;
        if (source.X < sourceBounds.Minimum.X ||
            source.X > sourceBounds.Maximum.X ||
            source.Y < sourceBounds.Minimum.Y ||
            source.Y > sourceBounds.Maximum.Y ||
            source.Z < sourceBounds.Minimum.Z ||
            source.Z > sourceBounds.Maximum.Z)
        {
            result.Message = "Scale source lies outside its selection bounds.";
            result.Destinations.clear();
            return result;
        }

        Position base = source;
        if ((mode == VoxelScaleMode::X || mode == VoxelScaleMode::Uniform) &&
            !ScaleCoordinate(source.X, sourceBounds.Minimum.X, base.X))
        {
            result.Message = "Scale X destination is not representable.";
            result.Destinations.clear();
            return result;
        }
        if ((mode == VoxelScaleMode::Y || mode == VoxelScaleMode::Uniform) &&
            !ScaleCoordinate(source.Y, sourceBounds.Minimum.Y, base.Y))
        {
            result.Message = "Scale Y destination is not representable.";
            result.Destinations.clear();
            return result;
        }
        if ((mode == VoxelScaleMode::Z || mode == VoxelScaleMode::Uniform) &&
            !ScaleCoordinate(source.Z, sourceBounds.Minimum.Z, base.Z))
        {
            result.Message = "Scale Z destination is not representable.";
            result.Destinations.clear();
            return result;
        }

        const std::int32_t xCopies =
            mode == VoxelScaleMode::X || mode == VoxelScaleMode::Uniform ? 2 : 1;
        const std::int32_t yCopies =
            mode == VoxelScaleMode::Y || mode == VoxelScaleMode::Uniform ? 2 : 1;
        const std::int32_t zCopies =
            mode == VoxelScaleMode::Z || mode == VoxelScaleMode::Uniform ? 2 : 1;
        for (std::int32_t dz = 0; dz < zCopies; ++dz)
            for (std::int32_t dy = 0; dy < yCopies; ++dy)
                for (std::int32_t dx = 0; dx < xCopies; ++dx)
                {
                    Position destination{};
                    if (!Increment(base.X, dx, destination.X) ||
                        !Increment(base.Y, dy, destination.Y) ||
                        !Increment(base.Z, dz, destination.Z))
                    {
                        result.Message =
                            "Scale destination is not representable.";
                        result.Destinations.clear();
                        return result;
                    }
                    result.Destinations.push_back({
                        source, destination, sourceVoxel.Value});
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
    }

    if (result.Destinations.size() != destinationCount)
    {
        result.Message = "Scale destination count is incomplete.";
        result.Destinations.clear();
        return result;
    }
    std::vector<Position> unique;
    unique.reserve(result.Destinations.size());
    for (const auto& destination : result.Destinations)
        unique.push_back(destination.DestinationPosition);
    std::sort(unique.begin(), unique.end(), PositionLess);
    if (std::adjacent_find(unique.begin(), unique.end()) != unique.end())
    {
        result.Message = "Scale destinations are not unique.";
        result.Destinations.clear();
        return result;
    }
    result.Bounds = SelectionBounds::FromCorners(minimum, maximum);
    return result;
}

ScaleVoxelSelectionResult ScaleVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    const VoxelScaleMode mode)
{
    if (!preview.IsActive() || !preview.HasExpandedDestinations())
        return Refused(ScaleVoxelSelectionResultCode::InvalidPreview,
            "Scale preview is inactive.");
    TransformOperationBuildResult common = TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview,
        {"Scale", std::string("Scale Voxels ") +
            VoxelScaleModeName(mode) + " x2",
         {TransformSourcePolicy::RemoveSource,
          TransformCollisionPolicy::AllowSourceOverlap},
         preview.PreviewBounds()});
    if (common.Code != TransformOperationBuildCode::Ready &&
        common.Code != TransformOperationBuildCode::NoChange)
        return FromCommon(std::move(common));
    const TransformPreviewOperationData data = preview.OperationData();
    const VoxelScaleGeometry geometry = BuildGeometry(
        data.SourceVoxels, data.SourceBounds, mode);
    if (!geometry.Valid() ||
        geometry.Destinations.size() != data.Voxels.size())
        return Refused(ScaleVoxelSelectionResultCode::InvalidGeometry,
            geometry.Message.empty()
                ? "Scale geometry is invalid." : geometry.Message);
    for (std::size_t index = 0U; index < data.Voxels.size(); ++index)
    {
        const TransformPreviewVoxel& voxel = data.Voxels[index];
        const TransformPreviewDestinationVoxel& expected =
            geometry.Destinations[index];
        if (voxel.SourcePosition != expected.SourcePosition ||
            voxel.PreviewPosition != expected.DestinationPosition ||
            voxel.Value != expected.Value)
            return Refused(ScaleVoxelSelectionResultCode::InvalidPreview,
                "Scale preview does not match its geometry.");
    }
    if (geometry.Bounds != preview.PreviewBounds())
        return Refused(ScaleVoxelSelectionResultCode::InvalidGeometry,
            "Scale destination bounds do not match the preview.");
    return FromCommon(std::move(common));
}

const char* VoxelScaleModeName(const VoxelScaleMode mode) noexcept
{
    switch (mode)
    {
    case VoxelScaleMode::X: return "X";
    case VoxelScaleMode::Y: return "Y";
    case VoxelScaleMode::Z: return "Z";
    case VoxelScaleMode::Uniform: return "Uniform";
    }
    return "X";
}

const char* ScaleVoxelSelectionResultCodeName(
    const ScaleVoxelSelectionResultCode code) noexcept
{
    switch (code)
    {
    case ScaleVoxelSelectionResultCode::Ready: return "Ready";
    case ScaleVoxelSelectionResultCode::InvalidGeometry: return "Invalid geometry";
    case ScaleVoxelSelectionResultCode::InvalidPreview: return "Invalid preview";
    case ScaleVoxelSelectionResultCode::ModelChanged: return "Model changed";
    case ScaleVoxelSelectionResultCode::SelectionChanged: return "Selection changed";
    case ScaleVoxelSelectionResultCode::Collision: return "Collision";
    case ScaleVoxelSelectionResultCode::OutOfBounds: return "Out of bounds";
    case ScaleVoxelSelectionResultCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
