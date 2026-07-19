#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class VoxelRotationDirection : std::uint8_t
{
    Clockwise,
    CounterClockwise
};

struct VoxelRotationGeometry final
{
    VoxelRotationDirection Direction = VoxelRotationDirection::Clockwise;
    // Centers use doubled voxel coordinates so half-cell shifts remain exact.
    std::int64_t SourceCenter2X = 0;
    std::int64_t SourceCenter2Z = 0;
    std::int64_t DestinationCenter2X = 0;
    std::int64_t DestinationCenter2Z = 0;
    std::int64_t Pivot2X = 0;
    std::int64_t Pivot2Z = 0;
    std::int64_t GridCorrection2X = 0;
    std::int64_t GridCorrection2Z = 0;
    std::vector<Asset::Voxel::VoxelPosition> Destinations;
    SelectionBounds Bounds{};
    std::string Message;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Message.empty() && !Destinations.empty() && Bounds.Valid;
    }
};

enum class RotateVoxelSelectionResultCode : std::uint8_t
{
    Ready,
    NoChange,
    InvalidGeometry,
    InvalidPreview,
    ModelChanged,
    SelectionChanged,
    Collision,
    OutOfBounds,
    Failed
};

struct RotateVoxelSelectionResult final
{
    RotateVoxelSelectionResultCode Code =
        RotateVoxelSelectionResultCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == RotateVoxelSelectionResultCode::Ready;
    }
};

class RotateVoxelSelectionOperation final
{
public:
    [[nodiscard]] static VoxelRotationGeometry BuildGeometry(
        std::span<const Asset::Voxel::VoxelPosition> sourcePositions,
        SelectionBounds sourceBounds,
        VoxelRotationDirection direction);

    [[nodiscard]] static RotateVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview,
        VoxelRotationDirection direction);
};

[[nodiscard]] const char* VoxelRotationDirectionName(
    VoxelRotationDirection direction) noexcept;
[[nodiscard]] const char* RotateVoxelSelectionResultCodeName(
    RotateVoxelSelectionResultCode code) noexcept;

} // namespace VoxelForge::Editor
