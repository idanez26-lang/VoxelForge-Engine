#include "TransformPanelViewModel.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
constexpr float IntegralTolerance = 0.001F;
constexpr float QuarterTurnDegrees = 90.0F;

bool IsFinite(const Vec3 value) noexcept
{
    return std::isfinite(value.X) && std::isfinite(value.Y) &&
        std::isfinite(value.Z);
}

std::array<std::uint32_t, 3U> Dimensions(
    const SelectionBounds& bounds) noexcept
{
    if (!bounds.Valid) return {};
    return {
        static_cast<std::uint32_t>(bounds.Maximum.X - bounds.Minimum.X + 1),
        static_cast<std::uint32_t>(bounds.Maximum.Y - bounds.Minimum.Y + 1),
        static_cast<std::uint32_t>(bounds.Maximum.Z - bounds.Minimum.Z + 1)};
}

std::int32_t NormalizeQuarterTurns(std::int32_t turns) noexcept
{
    turns %= 4;
    if (turns > 2) turns -= 4;
    if (turns < -2) turns += 4;
    return turns;
}

TransformPanelPositionEdit PositionRefused(
    const TransformPanelEditCode code,
    std::string message) noexcept
{
    return {code, {}, std::move(message)};
}

TransformPanelRotationEdit RotationRefused(
    const TransformPanelEditCode code,
    std::string message) noexcept
{
    return {code, VoxelRotationAxis::Y, 0, std::move(message)};
}

TransformPanelScaleEdit ScaleRefused(
    const TransformPanelEditCode code,
    std::string message) noexcept
{
    return {code, {}, std::move(message)};
}
}

TransformPanelState TransformPanelViewModel::Read(
    const TransformPanelSource& source) const noexcept
{
    TransformPanelState state;
    state.Available = source.Available && source.SourceBounds.Valid;
    state.PivotMode = source.Pivot.Mode;
    if (!state.Available) return state;

    state.Position = source.Pivot.WorldPosition;
    if (source.RotationPreview)
    {
        const float degrees = static_cast<float>(
            source.RotationPreview->QuarterTurns) * QuarterTurnDegrees;
        if (source.RotationPreview->Axis == VoxelRotationAxis::X)
            state.RotationDegrees.X = degrees;
        else if (source.RotationPreview->Axis == VoxelRotationAxis::Y)
            state.RotationDegrees.Y = degrees;
        else
            state.RotationDegrees.Z = degrees;
    }

    if (source.ScalePreviewDimensions)
    {
        const auto dimensions = Dimensions(source.SourceBounds);
        if (dimensions[0] != 0U && dimensions[1] != 0U &&
            dimensions[2] != 0U)
        {
            state.Scale = {
                static_cast<float>(source.ScalePreviewDimensions->X) /
                    static_cast<float>(dimensions[0]),
                static_cast<float>(source.ScalePreviewDimensions->Y) /
                    static_cast<float>(dimensions[1]),
                static_cast<float>(source.ScalePreviewDimensions->Z) /
                    static_cast<float>(dimensions[2])};
        }
    }
    return state;
}

TransformPanelPositionEdit TransformPanelViewModel::PreparePosition(
    const TransformPanelSource& source,
    const Vec3 requestedPosition) const noexcept
{
    const TransformPanelState current = Read(source);
    if (!current.Available)
        return PositionRefused(
            TransformPanelEditCode::Unavailable,
            "Select voxels before editing Position.");
    if (!IsFinite(requestedPosition))
        return PositionRefused(
            TransformPanelEditCode::InvalidValue,
            "Position values must be finite numbers.");

    const Vec3 difference = requestedPosition - current.Position;
    const std::array<float, 3U> values{
        difference.X, difference.Y, difference.Z};
    std::array<std::int32_t, 3U> delta{};
    for (std::size_t index = 0U; index < values.size(); ++index)
    {
        const double rounded = std::round(static_cast<double>(values[index]));
        if (rounded < std::numeric_limits<std::int32_t>::min() ||
            rounded > std::numeric_limits<std::int32_t>::max() ||
            std::abs(static_cast<double>(values[index]) - rounded) >
                IntegralTolerance)
        {
            return PositionRefused(
                TransformPanelEditCode::InvalidValue,
                "Voxel Position must resolve to whole-cell movement.");
        }
        delta[index] = static_cast<std::int32_t>(rounded);
    }
    if (delta[0] == 0 && delta[1] == 0 && delta[2] == 0)
        return PositionRefused(
            TransformPanelEditCode::NoChange,
            "Position already matches the requested value.");
    return {TransformPanelEditCode::Ready,
        {delta[0], delta[1], delta[2]}, {}};
}

TransformPanelRotationEdit TransformPanelViewModel::PrepareRotation(
    const TransformPanelSource& source,
    const Vec3 requestedDegrees) const noexcept
{
    if (!Read(source).Available)
        return RotationRefused(
            TransformPanelEditCode::Unavailable,
            "Select voxels before editing Rotation.");
    if (!IsFinite(requestedDegrees))
        return RotationRefused(
            TransformPanelEditCode::InvalidValue,
            "Rotation values must be finite numbers.");

    const std::array<float, 3U> values{
        requestedDegrees.X, requestedDegrees.Y, requestedDegrees.Z};
    std::array<std::int32_t, 3U> turns{};
    std::size_t activeAxes = 0U;
    std::size_t activeAxis = 0U;
    for (std::size_t index = 0U; index < values.size(); ++index)
    {
        const double rawTurns =
            static_cast<double>(values[index]) / QuarterTurnDegrees;
        const double rounded = std::round(rawTurns);
        if (rounded < std::numeric_limits<std::int32_t>::min() ||
            rounded > std::numeric_limits<std::int32_t>::max() ||
            std::abs(rawTurns - rounded) > IntegralTolerance)
        {
            return RotationRefused(
                TransformPanelEditCode::UnsupportedRotation,
                "Voxel Rotation supports exact 90-degree increments.");
        }
        turns[index] = NormalizeQuarterTurns(
            static_cast<std::int32_t>(rounded));
        if (turns[index] != 0)
        {
            ++activeAxes;
            activeAxis = index;
        }
    }
    if (activeAxes == 0U)
        return RotationRefused(
            TransformPanelEditCode::NoChange,
            "Rotation is already zero.");
    if (activeAxes != 1U)
        return RotationRefused(
            TransformPanelEditCode::UnsupportedRotation,
            "Edit one voxel rotation axis at a time.");
    const VoxelRotationAxis axis = activeAxis == 0U
        ? VoxelRotationAxis::X
        : activeAxis == 1U ? VoxelRotationAxis::Y : VoxelRotationAxis::Z;
    return {TransformPanelEditCode::Ready, axis, turns[activeAxis], {}};
}

TransformPanelScaleEdit TransformPanelViewModel::PrepareScale(
    const TransformPanelSource& source,
    const Vec3 requestedScale) const noexcept
{
    if (!Read(source).Available)
        return ScaleRefused(
            TransformPanelEditCode::Unavailable,
            "Select voxels before editing Scale.");
    if (!IsFinite(requestedScale) || requestedScale.X <= 0.0F ||
        requestedScale.Y <= 0.0F || requestedScale.Z <= 0.0F)
    {
        return ScaleRefused(
            TransformPanelEditCode::InvalidValue,
            "Scale values must be finite and greater than zero.");
    }

    const auto sourceDimensions = Dimensions(source.SourceBounds);
    const std::array<float, 3U> values{
        requestedScale.X, requestedScale.Y, requestedScale.Z};
    std::array<std::uint32_t, 3U> target{};
    for (std::size_t index = 0U; index < values.size(); ++index)
    {
        const double scaled = static_cast<double>(sourceDimensions[index]) *
            static_cast<double>(values[index]);
        const double rounded = std::round(scaled);
        if (rounded < 1.0 ||
            rounded > Asset::Vox::MaximumVoxDimension)
        {
            return ScaleRefused(
                TransformPanelEditCode::OutOfBounds,
                "Scale target must remain within the VOX dimension limit.");
        }
        target[index] = static_cast<std::uint32_t>(rounded);
    }
    if (target == sourceDimensions)
        return ScaleRefused(
            TransformPanelEditCode::NoChange,
            "Scale already matches the requested value.");
    return {TransformPanelEditCode::Ready,
        {target[0], target[1], target[2]}, {}};
}

const char* TransformPanelViewModel::PivotModeName(
    const TransformPivotMode mode) noexcept
{
    switch (mode)
    {
    case TransformPivotMode::Center: return "Center";
    case TransformPivotMode::Bottom: return "Bottom";
    case TransformPivotMode::Top: return "Top";
    }
    return "Center";
}

const char* TransformPanelEditCodeName(
    const TransformPanelEditCode code) noexcept
{
    switch (code)
    {
    case TransformPanelEditCode::Ready: return "Ready";
    case TransformPanelEditCode::NoChange: return "No change";
    case TransformPanelEditCode::Unavailable: return "Unavailable";
    case TransformPanelEditCode::InvalidValue: return "Invalid value";
    case TransformPanelEditCode::UnsupportedRotation:
        return "Unsupported rotation";
    case TransformPanelEditCode::OutOfBounds: return "Out of bounds";
    }
    return "Unavailable";
}

} // namespace VoxelForge::Editor
