#include "TransformGizmoModel.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<float, 4U> XAxisColor{0.94F, 0.20F, 0.18F, 1.0F};
constexpr std::array<float, 4U> YAxisColor{0.24F, 0.86F, 0.32F, 1.0F};
constexpr std::array<float, 4U> ZAxisColor{0.20F, 0.46F, 1.0F, 1.0F};
constexpr float MinimumDepth = 0.05F;
constexpr float MinimumWorldLength = 0.001F;
constexpr float MaximumWorldLength = 10000.0F;

[[nodiscard]] bool IsFinite(const Vec3 value) noexcept
{
    return std::isfinite(value.X) && std::isfinite(value.Y) &&
        std::isfinite(value.Z);
}

[[nodiscard]] TransformGizmoView HiddenView() noexcept
{
    return {};
}
}

TransformGizmoMode TransformGizmoModel::ModeForTool(
    const ActiveVoxelTool tool) noexcept
{
    switch (tool)
    {
    case ActiveVoxelTool::Move: return TransformGizmoMode::Move;
    case ActiveVoxelTool::Rotate: return TransformGizmoMode::Rotate;
    case ActiveVoxelTool::Scale: return TransformGizmoMode::Scale;
    default: return TransformGizmoMode::None;
    }
}

float TransformGizmoModel::CalculateWorldAxisLength(
    const TransformGizmoUpdateContext& context,
    const Vec3 worldCenter) noexcept
{
    if (!IsFinite(worldCenter) || !IsFinite(context.CameraPosition) ||
        !IsFinite(context.CameraForward) ||
        !std::isfinite(context.ViewportHeightPixels) ||
        context.ViewportHeightPixels <= 0.0F)
        return 0.0F;

    float worldLength = 0.0F;
    if (context.Projection == TransformGizmoProjection::Orthographic)
    {
        if (!std::isfinite(context.OrthographicWorldHeight) ||
            context.OrthographicWorldHeight <= 0.0F)
            return 0.0F;
        worldLength = DesiredAxisLengthPixels *
            context.OrthographicWorldHeight / context.ViewportHeightPixels;
    }
    else
    {
        if (!std::isfinite(context.VerticalFieldOfViewDegrees) ||
            context.VerticalFieldOfViewDegrees <= 1.0F ||
            context.VerticalFieldOfViewDegrees >= 179.0F)
            return 0.0F;
        const Vec3 forward = Normalize(context.CameraForward);
        if (Length(forward) <= 0.00001F) return 0.0F;
        const float depth = Dot(worldCenter - context.CameraPosition, forward);
        if (!std::isfinite(depth) || depth <= MinimumDepth) return 0.0F;
        const float halfFov = DegreesToRadians(
            context.VerticalFieldOfViewDegrees) * 0.5F;
        worldLength = DesiredAxisLengthPixels * depth * 2.0F *
            std::tan(halfFov) / context.ViewportHeightPixels;
    }
    if (!std::isfinite(worldLength) || worldLength <= 0.0F) return 0.0F;
    return std::clamp(
        worldLength, MinimumWorldLength, MaximumWorldLength);
}

bool TransformGizmoModel::Update(
    const TransformGizmoUpdateContext& context) noexcept
{
    const TransformGizmoMode mode = ModeForTool(context.ActiveTool);
    if (!context.DocumentActive || context.SelectionEmpty || context.Closing ||
        context.ActiveDocumentGeneration == 0U ||
        context.SelectionDocumentGeneration !=
            context.ActiveDocumentGeneration ||
        !context.Bounds.Valid || mode == TransformGizmoMode::None)
    {
        const TransformGizmoView hidden = HiddenView();
        if (view_ == hidden) return false;
        view_ = hidden;
        return true;
    }

    // Voxel bounds are inclusive integer cell coordinates. The spatial box is
    // [minimum, maximum + 1], then translated by the same model center used by
    // the viewport mesh and every existing selection overlay.
    const Vec3 gridCenter{
        (static_cast<float>(context.Bounds.Minimum.X) +
         static_cast<float>(context.Bounds.Maximum.X) + 1.0F) * 0.5F,
        (static_cast<float>(context.Bounds.Minimum.Y) +
         static_cast<float>(context.Bounds.Maximum.Y) + 1.0F) * 0.5F,
        (static_cast<float>(context.Bounds.Minimum.Z) +
         static_cast<float>(context.Bounds.Maximum.Z) + 1.0F) * 0.5F};
    const Vec3 worldCenter = gridCenter - context.ModelCenter;
    const float axisLength = CalculateWorldAxisLength(context, worldCenter);
    if (axisLength <= 0.0F)
    {
        const TransformGizmoView hidden = HiddenView();
        if (view_ == hidden) return false;
        view_ = hidden;
        return true;
    }

    TransformGizmoView next;
    next.Visible = true;
    next.Mode = mode;
    next.State = TransformGizmoInteractionState::Idle;
    next.Center = worldCenter;
    next.AxisLength = axisLength;
    next.AxisThickness = axisLength * 0.028F;
    next.CenterRadius = axisLength * 0.065F;
    next.Axes = {{
        {TransformGizmoAxis::X, worldCenter,
         worldCenter + Vec3{axisLength, 0.0F, 0.0F}, XAxisColor},
        {TransformGizmoAxis::Y, worldCenter,
         worldCenter + Vec3{0.0F, axisLength, 0.0F}, YAxisColor},
        {TransformGizmoAxis::Z, worldCenter,
         worldCenter + Vec3{0.0F, 0.0F, axisLength}, ZAxisColor}}};
    if (view_ == next) return false;
    view_ = next;
    return true;
}

void TransformGizmoModel::Reset() noexcept
{
    view_ = HiddenView();
}

const TransformGizmoView& TransformGizmoModel::View() const noexcept
{
    return view_;
}

} // namespace VoxelForge::Editor
