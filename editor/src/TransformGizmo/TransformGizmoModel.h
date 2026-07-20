#pragma once

#include "EditorMath.h"
#include "Selection/SelectionService.h"
#include "VoxelTools/VoxelToolState.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor
{

enum class TransformGizmoMode : std::uint8_t
{
    None,
    Move,
    Rotate,
    Scale
};

enum class TransformGizmoAxis : std::uint8_t
{
    None,
    X,
    Y,
    Z
};

enum class TransformGizmoInteractionState : std::uint8_t
{
    Hidden,
    Idle,
    Hover,
    Dragging
};

enum class TransformGizmoProjection : std::uint8_t
{
    Perspective,
    Orthographic
};

struct TransformGizmoAxisView final
{
    TransformGizmoAxis Axis = TransformGizmoAxis::None;
    Vec3 Start{};
    Vec3 End{};
    std::array<float, 4U> Color{};

    [[nodiscard]] bool operator==(
        const TransformGizmoAxisView&) const noexcept = default;
};

struct TransformGizmoView final
{
    bool Visible = false;
    TransformGizmoMode Mode = TransformGizmoMode::None;
    TransformGizmoAxis ActiveAxis = TransformGizmoAxis::None;
    TransformGizmoInteractionState State =
        TransformGizmoInteractionState::Hidden;
    Vec3 Center{};
    float AxisLength = 0.0F;
    float AxisThickness = 0.0F;
    float CenterRadius = 0.0F;
    std::array<TransformGizmoAxisView, 3U> Axes{};

    [[nodiscard]] bool operator==(
        const TransformGizmoView&) const noexcept = default;
};

struct TransformGizmoUpdateContext final
{
    bool DocumentActive = false;
    bool SelectionEmpty = true;
    bool Closing = false;
    std::uint64_t ActiveDocumentGeneration = 0U;
    std::uint64_t SelectionDocumentGeneration = 0U;
    SelectionBounds Bounds{};
    ActiveVoxelTool ActiveTool = ActiveVoxelTool::None;
    Vec3 ModelCenter{};
    Vec3 CameraPosition{};
    Vec3 CameraForward{0.0F, 0.0F, 1.0F};
    float VerticalFieldOfViewDegrees = 45.0F;
    float ViewportHeightPixels = 0.0F;
    TransformGizmoProjection Projection =
        TransformGizmoProjection::Perspective;
    float OrthographicWorldHeight = 0.0F;
};

class TransformGizmoModel final
{
public:
    static constexpr float DesiredAxisLengthPixels = 90.0F;
    static constexpr std::size_t AxisPrimitiveCount = 3U;
    static constexpr std::size_t TotalPrimitiveCount = 4U;

    [[nodiscard]] bool Update(
        const TransformGizmoUpdateContext& context) noexcept;
    void Reset() noexcept;

    [[nodiscard]] const TransformGizmoView& View() const noexcept;
    [[nodiscard]] static TransformGizmoMode ModeForTool(
        ActiveVoxelTool tool) noexcept;
    [[nodiscard]] static float CalculateWorldAxisLength(
        const TransformGizmoUpdateContext& context,
        Vec3 worldCenter) noexcept;

private:
    TransformGizmoView view_{};
};

} // namespace VoxelForge::Editor
