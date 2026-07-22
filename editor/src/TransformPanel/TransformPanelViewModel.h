#pragma once

#include "Selection/SelectionService.h"
#include "Transform/RotateVoxelSelectionOperation.h"
#include "Transform/TransformPivot.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <optional>
#include <string>

namespace VoxelForge::Editor
{

struct TransformPanelRotation final
{
    VoxelRotationAxis Axis = VoxelRotationAxis::Y;
    std::int32_t QuarterTurns = 0;
};

struct TransformPanelSource final
{
    bool Available = false;
    TransformPivot Pivot{};
    SelectionBounds SourceBounds{};
    std::optional<TransformPanelRotation> RotationPreview;
    std::optional<Asset::Voxel::VoxelDimensions> ScalePreviewDimensions;
};

struct TransformPanelState final
{
    bool Available = false;
    Vec3 Position{};
    Vec3 RotationDegrees{};
    Vec3 Scale{1.0F, 1.0F, 1.0F};
    TransformPivotMode PivotMode = TransformPivotMode::Center;
};

enum class TransformPanelEditCode : std::uint8_t
{
    Ready,
    NoChange,
    Unavailable,
    InvalidValue,
    UnsupportedRotation,
    OutOfBounds
};

struct TransformPanelPositionEdit final
{
    TransformPanelEditCode Code = TransformPanelEditCode::Unavailable;
    Asset::Voxel::VoxelPosition Delta{};
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == TransformPanelEditCode::Ready;
    }
};

struct TransformPanelRotationEdit final
{
    TransformPanelEditCode Code = TransformPanelEditCode::Unavailable;
    VoxelRotationAxis Axis = VoxelRotationAxis::Y;
    std::int32_t QuarterTurns = 0;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == TransformPanelEditCode::Ready;
    }
};

struct TransformPanelScaleEdit final
{
    TransformPanelEditCode Code = TransformPanelEditCode::Unavailable;
    Asset::Voxel::VoxelDimensions TargetDimensions{};
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == TransformPanelEditCode::Ready;
    }
};

// Stateless numerical facade over the transform framework. Values are rebuilt
// from the current selection, preview, and shared pivot every frame.
class TransformPanelViewModel final
{
public:
    [[nodiscard]] TransformPanelState Read(
        const TransformPanelSource& source) const noexcept;
    [[nodiscard]] TransformPanelPositionEdit PreparePosition(
        const TransformPanelSource& source,
        Vec3 requestedPosition) const noexcept;
    [[nodiscard]] TransformPanelRotationEdit PrepareRotation(
        const TransformPanelSource& source,
        Vec3 requestedDegrees) const noexcept;
    [[nodiscard]] TransformPanelScaleEdit PrepareScale(
        const TransformPanelSource& source,
        Vec3 requestedScale) const noexcept;

    [[nodiscard]] static const char* PivotModeName(
        TransformPivotMode mode) noexcept;
};

[[nodiscard]] const char* TransformPanelEditCodeName(
    TransformPanelEditCode code) noexcept;

} // namespace VoxelForge::Editor
