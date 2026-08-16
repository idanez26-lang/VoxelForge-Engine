#include "ScaleVoxelSelectionOperation.h"
#include "TransformOperationFramework.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"

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

[[nodiscard]] std::uint64_t DivideRoundUp(
    const std::uint64_t numerator,
    const std::uint64_t denominator) noexcept
{
    return numerator / denominator +
        static_cast<std::uint64_t>(numerator % denominator != 0U);
}

[[nodiscard]] bool AddOffset(
    const std::int32_t minimum,
    const std::uint64_t offset,
    std::int32_t& destination) noexcept
{
    const std::int64_t result =
        static_cast<std::int64_t>(minimum) +
        static_cast<std::int64_t>(offset);
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

[[nodiscard]] Asset::Voxel::VoxelDimensions DoubledDimensions(
    const SelectionBounds bounds, const VoxelScaleMode mode) noexcept
{
    Asset::Voxel::VoxelDimensions dimensions = bounds.Dimensions();
    if (mode == VoxelScaleMode::X || mode == VoxelScaleMode::Uniform)
        dimensions.X *= 2U;
    if (mode == VoxelScaleMode::Y || mode == VoxelScaleMode::Uniform)
        dimensions.Y *= 2U;
    if (mode == VoxelScaleMode::Z || mode == VoxelScaleMode::Uniform)
        dimensions.Z *= 2U;
    return dimensions;
}
}

VoxelScaleGeometry ScaleVoxelSelectionOperation::BuildGeometry(
    const std::span<const TransformPreviewVoxel> sourceVoxels,
    const SelectionBounds sourceBounds,
    const VoxelScaleMode mode)
{
    return BuildGeometry(
        sourceVoxels, sourceBounds, mode,
        DoubledDimensions(sourceBounds, mode));
}

VoxelScaleGeometry ScaleVoxelSelectionOperation::BuildGeometry(
    const std::span<const TransformPreviewVoxel> sourceVoxels,
    const SelectionBounds sourceBounds,
    const VoxelScaleMode mode,
    const Asset::Voxel::VoxelDimensions targetDimensions)
{
    VoxelScaleGeometry result;
    result.Mode = mode;
    result.SourceCount = sourceVoxels.size();
    if (sourceVoxels.empty() || !sourceBounds.Valid)
    {
        result.Message = "Scale requires a non-empty selection.";
        return result;
    }
    const Asset::Voxel::VoxelDimensions sourceDimensions =
        sourceBounds.Dimensions();
    if (sourceDimensions.X == 0U || sourceDimensions.Y == 0U ||
        sourceDimensions.Z == 0U || targetDimensions.X == 0U ||
        targetDimensions.Y == 0U || targetDimensions.Z == 0U)
    {
        result.Message = "Scale dimensions must remain at least one voxel.";
        return result;
    }
    if (targetDimensions.X > Asset::Vox::MaximumVoxDimension ||
        targetDimensions.Y > Asset::Vox::MaximumVoxDimension ||
        targetDimensions.Z > Asset::Vox::MaximumVoxDimension)
    {
        result.Message = "Scale target exceeds VOX dimension limits.";
        return result;
    }
    std::uint64_t maximumDestinationCount = targetDimensions.X;
    if (maximumDestinationCount >
            std::numeric_limits<std::uint64_t>::max() / targetDimensions.Y)
    {
        result.Message = "Scale destination count is not representable.";
        return result;
    }
    maximumDestinationCount *= targetDimensions.Y;
    if (maximumDestinationCount >
            std::numeric_limits<std::uint64_t>::max() / targetDimensions.Z)
    {
        result.Message = "Scale destination count is not representable.";
        return result;
    }
    maximumDestinationCount *= targetDimensions.Z;
    if (maximumDestinationCount > result.Destinations.max_size())
    {
        result.Message = "Scale destination count exceeds container limits.";
        return result;
    }

    result.Destinations.reserve(static_cast<std::size_t>(
        std::min<std::uint64_t>(maximumDestinationCount,
            sourceVoxels.size() * static_cast<std::uint64_t>(
                Multiplier(mode)))));
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

        const std::array<std::uint64_t, 3U> local{
            static_cast<std::uint64_t>(source.X - sourceBounds.Minimum.X),
            static_cast<std::uint64_t>(source.Y - sourceBounds.Minimum.Y),
            static_cast<std::uint64_t>(source.Z - sourceBounds.Minimum.Z)};
        const std::array<std::uint64_t, 3U> sourceSize{
            sourceDimensions.X, sourceDimensions.Y, sourceDimensions.Z};
        const std::array<std::uint64_t, 3U> targetSize{
            targetDimensions.X, targetDimensions.Y, targetDimensions.Z};
        std::array<std::uint64_t, 3U> firstTarget{};
        std::array<std::uint64_t, 3U> endTarget{};
        for (std::size_t axis = 0U; axis < 3U; ++axis)
        {
            firstTarget[axis] = DivideRoundUp(
                local[axis] * targetSize[axis], sourceSize[axis]);
            endTarget[axis] = DivideRoundUp(
                (local[axis] + 1U) * targetSize[axis], sourceSize[axis]);
        }
        for (std::uint64_t z = firstTarget[2]; z < endTarget[2]; ++z)
            for (std::uint64_t y = firstTarget[1]; y < endTarget[1]; ++y)
                for (std::uint64_t x = firstTarget[0]; x < endTarget[0]; ++x)
                {
                    Position destination{};
                    if (!AddOffset(sourceBounds.Minimum.X, x, destination.X) ||
                        !AddOffset(sourceBounds.Minimum.Y, y, destination.Y) ||
                        !AddOffset(sourceBounds.Minimum.Z, z, destination.Z))
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

    if (result.Destinations.empty())
    {
        result.Message = "Scale produced no destination voxels.";
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
    return Build(document, selection, documentGeneration, preview, mode,
        DoubledDimensions(preview.SourceBounds(), mode));
}

ScaleVoxelSelectionResult ScaleVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    const VoxelScaleMode mode,
    const Asset::Voxel::VoxelDimensions targetDimensions)
{
    if (!preview.IsActive() || !preview.HasExpandedDestinations())
        return Refused(ScaleVoxelSelectionResultCode::InvalidPreview,
            "Scale preview is inactive.");
    TransformOperationBuildResult common = TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview,
        {"Scale", std::string("Scale Voxels ") +
            VoxelScaleModeName(mode) + " to " +
            std::to_string(targetDimensions.X) + "x" +
            std::to_string(targetDimensions.Y) + "x" +
            std::to_string(targetDimensions.Z),
         {TransformSourcePolicy::RemoveSource,
          TransformCollisionPolicy::MergeOverlap},
         preview.PreviewBounds()});
    if (common.Code != TransformOperationBuildCode::Ready &&
        common.Code != TransformOperationBuildCode::NoChange)
        return FromCommon(std::move(common));
    const TransformPreviewOperationData data = preview.OperationData();
    const VoxelScaleGeometry geometry = BuildGeometry(
        data.SourceVoxels, data.SourceBounds, mode, targetDimensions);
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
