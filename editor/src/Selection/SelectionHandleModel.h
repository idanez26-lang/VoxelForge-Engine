#pragma once

#include "SelectionService.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include <array>
#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

enum class SelectionFace : std::uint8_t
{
    None,
    XMinimum,
    XMaximum,
    YMinimum,
    YMaximum,
    ZMinimum,
    ZMaximum
};

struct SelectionHandle final
{
    SelectionFace Face = SelectionFace::None;
    Vec3 WorldPosition{};
    Vec2 ScreenPosition{};
    Vec2 ScreenAxisPerVoxel{};
    float Depth = 0.0F;
    bool Visible = false;
};

using SelectionHandles = std::array<SelectionHandle, 6U>;

[[nodiscard]] SelectionHandles GenerateSelectionHandles(
    const SelectionBounds& bounds,
    Vec3 modelCenter = {}) noexcept;

[[nodiscard]] SelectionHandles ProjectSelectionHandles(
    const SelectionBounds& bounds,
    Vec3 modelCenter,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept;

[[nodiscard]] std::optional<SelectionHandle> PickSelectionHandle(
    const SelectionHandles& handles,
    Vec2 pointerScreenPosition,
    float radiusPixels = 10.0F) noexcept;

[[nodiscard]] SelectionBounds ResizeSelectionBounds(
    const SelectionBounds& original,
    SelectionFace face,
    std::int32_t gridDelta,
    Asset::Voxel::VoxelDimensions documentDimensions) noexcept;

} // namespace VoxelForge::Editor
