#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class VoxelMirrorAxis : std::uint8_t
{
    X,
    Z
};

struct VoxelMirrorGeometry final
{
    VoxelMirrorAxis Axis = VoxelMirrorAxis::X;
    std::vector<Asset::Voxel::VoxelPosition> Destinations;
    SelectionBounds Bounds{};
    bool Identity = false;
    std::string Message;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Message.empty() && !Destinations.empty() && Bounds.Valid;
    }
};

enum class MirrorVoxelSelectionResultCode : std::uint8_t
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

struct MirrorVoxelSelectionResult final
{
    MirrorVoxelSelectionResultCode Code =
        MirrorVoxelSelectionResultCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == MirrorVoxelSelectionResultCode::Ready;
    }
};

class MirrorVoxelSelectionOperation final
{
public:
    [[nodiscard]] static VoxelMirrorGeometry BuildGeometry(
        std::span<const Asset::Voxel::VoxelPosition> sourcePositions,
        SelectionBounds sourceBounds,
        VoxelMirrorAxis axis);

    [[nodiscard]] static MirrorVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview,
        VoxelMirrorAxis axis);
};

[[nodiscard]] const char* VoxelMirrorAxisName(VoxelMirrorAxis axis) noexcept;
[[nodiscard]] const char* MirrorVoxelSelectionResultCodeName(
    MirrorVoxelSelectionResultCode code) noexcept;

} // namespace VoxelForge::Editor
