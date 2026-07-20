#pragma once

#include "EditorMatrix.h"
#include "Selection/SelectionService.h"
#include "TransformGizmo/GizmoStyle.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelTools/VoxelToolState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

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
    static constexpr std::size_t RotationRingSegmentCount = 128U;

    TransformGizmoAxis Axis = TransformGizmoAxis::None;
    Vec3 Start{};
    Vec3 End{};
    std::array<float, 4U> Color{};
    float Thickness = 0.0F;
    float ProjectedLengthPixels = 0.0F;
    bool CameraFacing = false;
    bool HasArrowHead = false;
    Vec3 ArrowBaseCenter{};
    std::array<Vec3, 4U> ArrowBaseCorners{};
    float ArrowLength = 0.0F;
    float ArrowWidth = 0.0F;
    bool HasRotationRing = false;
    float RotationRingRadius = 0.0F;
    std::array<Vec3, RotationRingSegmentCount> RotationRingPoints{};

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
    TransformGizmoInteractionState InteractionState =
        TransformGizmoInteractionState::Idle;
    TransformGizmoAxis ActiveAxis = TransformGizmoAxis::None;
    Matrix4 ViewProjection = IdentityMatrix();
    ViewportRectangle Viewport{};
};

struct TransformGizmoSizingResult final
{
    bool Visible = false;
    float CameraDepth = 0.0F;
    float UnclampedWorldLength = 0.0F;
    float WorldLength = 0.0F;
    float ProjectedLengthPixels = 0.0F;
    float SelectionMaximumExtent = 0.0F;
    bool MinimumClampApplied = false;
    bool MaximumClampApplied = false;
    bool CorrectionMinimumClampApplied = false;
    bool CorrectionMaximumClampApplied = false;
    bool CameraFacing = false;
    std::uint8_t CorrectionIterations = 0U;
};

struct TransformGizmoRenderPolicy final
{
    static constexpr bool VisiblePassDepthTestEnabled = true;
    static constexpr bool VisiblePassDepthWriteEnabled = false;
    static constexpr bool OccludedPassDepthTestEnabled = true;
    static constexpr bool OccludedPassDepthWriteEnabled = false;
    static constexpr float OccludedColorScale = 0.48F;
    static constexpr float OccludedAlpha = 0.34F;
    static constexpr float OccludedThicknessScale = 0.82F;
    static constexpr bool CenterScreenOverlayEnabled = true;
};

class TransformGizmoModel final
{
public:
    static constexpr float SelectionRelativeFactor = 0.35F;
    static constexpr float AxisCeilingTargetPixels = 100.0F;
    static constexpr float MaximumAxisLengthPixels = 110.0F;
    static constexpr float MinimumPositiveDepth = 0.0001F;
    static constexpr float MinimumWorldLength = 0.75F;
    static constexpr float MaximumWorldLength = 18.0F;
    static constexpr float MinimumScreenCappedWorldLength = 0.0001F;
    static constexpr float MinimumCorrectionFactor = 0.001F;
    static constexpr float MaximumCorrectionFactor = 1.0F;
    static constexpr float MinimumAxisViewSine = 0.15F;
    static constexpr float ArrowLengthRatio =
        GizmoStyle::MoveArrowLengthRatio;
    static constexpr float MinimumArrowLengthPixels = 6.0F;
    static constexpr float MaximumArrowLengthPixels = 14.0F;
    static constexpr float MinimumArrowWidthPixels = 4.0F;
    static constexpr float MaximumArrowWidthPixels = 8.0F;
    static constexpr float MaximumArrowAxisRatio = 0.40F;
    static constexpr float NormalAxisThicknessPixels =
        GizmoStyle::MoveAxisIdleThicknessPixels;
    static constexpr float ActiveAxisThicknessPixels =
        GizmoStyle::MoveAxisHoverThicknessPixels;
    static constexpr float MinimumCenterPixels =
        GizmoStyle::MoveCenterDiameterPixels * 0.5F;
    static constexpr std::size_t AxisPrimitiveCount = 3U;
    static constexpr std::size_t ArrowPrimitiveCount = 3U;
    static constexpr std::size_t TotalPrimitiveCount = 7U;

    [[nodiscard]] bool Update(
        const TransformGizmoUpdateContext& context) noexcept;
    void Reset() noexcept;

    [[nodiscard]] const TransformGizmoView& View() const noexcept;
    [[nodiscard]] static TransformGizmoMode ModeForTool(
        ActiveVoxelTool tool) noexcept;
    [[nodiscard]] static float CalculateWorldAxisLength(
        const TransformGizmoUpdateContext& context,
        Vec3 worldCenter) noexcept;
    [[nodiscard]] static TransformGizmoSizingResult CalculateSizing(
        const TransformGizmoUpdateContext& context,
        Vec3 worldCenter,
        TransformGizmoAxis axis = TransformGizmoAxis::X) noexcept;
    [[nodiscard]] static std::optional<Vec2> ProjectWorldToScreen(
        Vec3 worldPosition,
        const ViewportRectangle& viewport,
        const Matrix4& viewProjection) noexcept;
    [[nodiscard]] static std::string_view ContextHelpFor(
        TransformGizmoInteractionState state,
        TransformGizmoAxis axis) noexcept;
    [[nodiscard]] static std::string_view ContextHelpFor(
        TransformGizmoMode mode,
        TransformGizmoInteractionState state,
        TransformGizmoAxis axis) noexcept;

private:
    TransformGizmoView view_{};
};

} // namespace VoxelForge::Editor
