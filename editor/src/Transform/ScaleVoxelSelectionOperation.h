#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class VoxelScaleMode : std::uint8_t
{
    X,
    Y,
    Z,
    Uniform
};

struct VoxelScaleGeometry final
{
    VoxelScaleMode Mode = VoxelScaleMode::X;
    std::vector<TransformPreviewDestinationVoxel> Destinations;
    SelectionBounds Bounds{};
    std::size_t SourceCount = 0U;
    std::string Message;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Message.empty() && SourceCount > 0U &&
            !Destinations.empty() && Bounds.Valid;
    }
};

enum class ScaleVoxelSelectionResultCode : std::uint8_t
{
    Ready,
    InvalidGeometry,
    InvalidPreview,
    ModelChanged,
    SelectionChanged,
    Collision,
    OutOfBounds,
    Failed
};

struct ScaleVoxelSelectionResult final
{
    ScaleVoxelSelectionResultCode Code =
        ScaleVoxelSelectionResultCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == ScaleVoxelSelectionResultCode::Ready;
    }
};

class ScaleVoxelSelectionOperation final
{
public:
    static constexpr std::size_t AxisFactor = 2U;
    static constexpr std::size_t UniformFactor = 8U;

    [[nodiscard]] static VoxelScaleGeometry BuildGeometry(
        std::span<const TransformPreviewVoxel> sourceVoxels,
        SelectionBounds sourceBounds,
        VoxelScaleMode mode);
    [[nodiscard]] static VoxelScaleGeometry BuildGeometry(
        std::span<const TransformPreviewVoxel> sourceVoxels,
        SelectionBounds sourceBounds,
        VoxelScaleMode mode,
        Asset::Voxel::VoxelDimensions targetDimensions);

    [[nodiscard]] static ScaleVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview,
        VoxelScaleMode mode);
    [[nodiscard]] static ScaleVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview,
        VoxelScaleMode mode,
        Asset::Voxel::VoxelDimensions targetDimensions);
};

[[nodiscard]] const char* VoxelScaleModeName(VoxelScaleMode mode) noexcept;
[[nodiscard]] const char* ScaleVoxelSelectionResultCodeName(
    ScaleVoxelSelectionResultCode code) noexcept;

} // namespace VoxelForge::Editor
