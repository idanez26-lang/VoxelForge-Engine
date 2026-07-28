#pragma once

#include "EditorMatrix.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include <array>
#include <cstdint>

namespace VoxelForge::Editor
{

enum class UniversalCursorPreviewSubject : std::uint8_t
{
    PencilSingleVoxel,
    Geometric
};

struct UniversalCursor2DTarget final
{
    Vec3 SurfaceWorldPosition{};
    Vec3 FaceNormal{};
};

struct UniversalCursor2DGeometry final
{
    std::array<Vec2, 4U> Corners{};
    bool Visible = false;
};

// Projects the four real corners of a one-voxel face. The returned geometry
// carries no depth and is intended for a readable screen-space overlay pass.
[[nodiscard]] UniversalCursor2DGeometry ProjectUniversalCursor2D(
    const UniversalCursor2DTarget& target,
    const ViewportRectangle& viewport,
    const Matrix4& viewProjection) noexcept;

[[nodiscard]] bool ShouldRenderExactPreviewGeometry(
    UniversalCursorPreviewSubject subject) noexcept;

} // namespace VoxelForge::Editor
