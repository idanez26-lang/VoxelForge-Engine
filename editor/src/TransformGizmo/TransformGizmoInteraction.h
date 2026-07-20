#pragma once

#include "EditorMatrix.h"
#include "Selection/SelectionService.h"
#include "TransformGizmoModel.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

struct TransformGizmoPointerInput final
{
    Vec2 ScreenPosition{};
    ViewportRectangle Viewport{};
    Matrix4 ViewProjection = IdentityMatrix();
    std::optional<VoxelRay> Ray;
};

struct TransformGizmoDragRelease final
{
    TransformGizmoAxis Axis = TransformGizmoAxis::None;
    Asset::Voxel::VoxelPosition Delta{};
    bool WasDragging = false;
};

class TransformGizmoInteraction final
{
public:
    static constexpr float PickTolerancePixels = 8.0F;
    static constexpr float MinimumProjectedAxisPixels = 4.0F;
    static constexpr float CameraFacingHandleRadiusPixels = 9.0F;

    [[nodiscard]] TransformGizmoAxis UpdateHover(
        const TransformGizmoView& view,
        const TransformGizmoPointerInput& input) noexcept;
    [[nodiscard]] bool BeginDrag(
        const TransformGizmoView& view,
        TransformGizmoAxis axis,
        const TransformGizmoPointerInput& input,
        std::uint64_t documentGeneration,
        SelectionBounds selectionBounds) noexcept;
    [[nodiscard]] bool UpdateDrag(
        const TransformGizmoPointerInput& input) noexcept;
    [[nodiscard]] TransformGizmoDragRelease EndDrag() noexcept;
    [[nodiscard]] bool Validate(
        std::uint64_t documentGeneration,
        SelectionBounds selectionBounds,
        bool moveToolActive) noexcept;
    [[nodiscard]] bool Cancel() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] TransformGizmoAxis HoveredAxis() const noexcept;
    [[nodiscard]] TransformGizmoAxis LockedAxis() const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelPosition Delta() const noexcept;
    [[nodiscard]] TransformGizmoInteractionState State() const noexcept;

private:
    [[nodiscard]] static Vec3 AxisVector(TransformGizmoAxis axis) noexcept;
    [[nodiscard]] static std::optional<float> ClosestAxisParameter(
        const VoxelRay& ray,
        Vec3 axisOrigin,
        Vec3 axisDirection) noexcept;
    [[nodiscard]] float ScreenFallbackParameter(Vec2 pointer) const noexcept;
    void ClearDrag() noexcept;

    TransformGizmoInteractionState state_ =
        TransformGizmoInteractionState::Idle;
    TransformGizmoAxis hoveredAxis_ = TransformGizmoAxis::None;
    TransformGizmoAxis lockedAxis_ = TransformGizmoAxis::None;
    Vec3 axisOrigin_{};
    Vec3 axisDirection_{};
    Vec2 pointerStart_{};
    Vec2 screenAxisDirection_{0.0F, -1.0F};
    float worldUnitsPerPixel_ = 0.0F;
    std::optional<float> rayAnchorParameter_;
    Asset::Voxel::VoxelPosition delta_{};
    std::uint64_t documentGeneration_ = 0U;
    SelectionBounds selectionBounds_{};
};

} // namespace VoxelForge::Editor
