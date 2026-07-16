#pragma once

#include "EditorMatrix.h"
#include "VoxelRay.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

struct ViewportRectangle final
{
    float X = 0.0F;
    float Y = 0.0F;
    float Width = 0.0F;
    float Height = 0.0F;
};

enum class ViewportRayBuildError : std::uint8_t
{
    None,
    InvalidInput,
    OutsideViewport,
    NonInvertibleViewProjection,
    InvalidResult
};

struct ViewportRayBuildResult final
{
    ViewportRayBuildError Error = ViewportRayBuildError::InvalidInput;
    std::optional<VoxelRay> Ray;

    [[nodiscard]] bool Succeeded() const noexcept
    {
        return Error == ViewportRayBuildError::None && Ray.has_value();
    }
};

// Screen -> viewport-local -> NDC -> world. The current renderer uses
// row-major matrices, column vectors and the Direct3D depth range [0, 1].
[[nodiscard]] ViewportRayBuildResult BuildViewportRay(
    Vec2 mouseScreenPosition,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection,
    Vec3 cameraWorldPosition) noexcept;

} // namespace VoxelForge::Editor
