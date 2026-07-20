#include "AlignVoxelSelectionOperation.h"

#include <limits>

namespace VoxelForge::Editor
{
namespace
{
bool FitsInt32(const std::uint32_t value) noexcept
{
    return value <= static_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::max());
}
}

std::optional<Asset::Voxel::VoxelPosition>
AlignVoxelSelectionOperation::CalculateDelta(
    const SelectionBounds selectionBounds,
    const Asset::Voxel::VoxelDimensions documentDimensions,
    const VoxelAlignDirection direction) noexcept
{
    if (!selectionBounds.Valid || documentDimensions.X == 0U ||
        documentDimensions.Y == 0U || documentDimensions.Z == 0U ||
        !FitsInt32(documentDimensions.X - 1U) ||
        !FitsInt32(documentDimensions.Y - 1U) ||
        !FitsInt32(documentDimensions.Z - 1U))
    {
        return std::nullopt;
    }

    const Asset::Voxel::VoxelPosition documentMaximum{
        static_cast<std::int32_t>(documentDimensions.X - 1U),
        static_cast<std::int32_t>(documentDimensions.Y - 1U),
        static_cast<std::int32_t>(documentDimensions.Z - 1U)};
    if (selectionBounds.Minimum.X < 0 || selectionBounds.Minimum.Y < 0 ||
        selectionBounds.Minimum.Z < 0 ||
        selectionBounds.Maximum.X > documentMaximum.X ||
        selectionBounds.Maximum.Y > documentMaximum.Y ||
        selectionBounds.Maximum.Z > documentMaximum.Z)
    {
        return std::nullopt;
    }

    Asset::Voxel::VoxelPosition delta{};
    switch (direction)
    {
    case VoxelAlignDirection::Left:
        delta.X = -selectionBounds.Minimum.X;
        break;
    case VoxelAlignDirection::Right:
        delta.X = documentMaximum.X - selectionBounds.Maximum.X;
        break;
    case VoxelAlignDirection::Bottom:
        delta.Y = -selectionBounds.Minimum.Y;
        break;
    case VoxelAlignDirection::Top:
        delta.Y = documentMaximum.Y - selectionBounds.Maximum.Y;
        break;
    case VoxelAlignDirection::Front:
        delta.Z = -selectionBounds.Minimum.Z;
        break;
    case VoxelAlignDirection::Back:
        delta.Z = documentMaximum.Z - selectionBounds.Maximum.Z;
        break;
    }
    return delta;
}

MoveVoxelSelectionResult AlignVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview)
{
    return MoveVoxelSelectionOperation::Build(
        document, selection, documentGeneration, preview);
}

const char* VoxelAlignDirectionName(
    const VoxelAlignDirection direction) noexcept
{
    switch (direction)
    {
    case VoxelAlignDirection::Left: return "Left";
    case VoxelAlignDirection::Right: return "Right";
    case VoxelAlignDirection::Bottom: return "Bottom";
    case VoxelAlignDirection::Top: return "Top";
    case VoxelAlignDirection::Front: return "Front";
    case VoxelAlignDirection::Back: return "Back";
    }
    return "Left";
}

} // namespace VoxelForge::Editor
