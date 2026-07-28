#pragma once

#include "EditorMatrix.h"
#include "VoxelSelection/ViewportRayBuilder.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace VoxelForge::Editor
{

enum class UniversalCursorPreviewSubject : std::uint8_t
{
    PencilSingleVoxel,
    PencilBrush,
    Geometric
};

enum class UniversalCursorAnchorPolicy : std::uint8_t
{
    PreferHoveredTarget,
    PreferPlannedTarget
};

struct UniversalCursor2DTarget final
{
    Vec3 SurfaceWorldPosition{};
    Vec3 FaceNormal{};

    [[nodiscard]] bool operator==(
        const UniversalCursor2DTarget&) const noexcept = default;
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
    UniversalCursorPreviewSubject subject,
    bool strokeActive) noexcept;

[[nodiscard]] bool ShouldRetainExactPreviewOnMissingFrame(
    UniversalCursorPreviewSubject subject,
    bool strokeActive) noexcept;

[[nodiscard]] bool ShouldResolvePreviewForPresentation(
    bool strokeActive) noexcept;

// Face Add can present the accepted immutable plan directly while dragging.
// This avoids rebuilding the complete final document mesh for every depth
// change. Paint, Erase, idle previews and every other geometry deliberately
// retain the exact final-state mesh path.
[[nodiscard]] bool ShouldPresentFaceAddAsPlanGhosts(
    bool faceGeometry,
    bool addAction,
    bool strokeActive) noexcept;

[[nodiscard]] std::optional<UniversalCursor2DTarget>
SelectUniversalCursor2DTarget(
    std::optional<UniversalCursor2DTarget> hovered,
    std::optional<UniversalCursor2DTarget> planned,
    UniversalCursorAnchorPolicy policy) noexcept;

[[nodiscard]] UniversalCursor2DTarget MakeVoxelFaceCursor2DTarget(
    Asset::Voxel::VoxelPosition voxel,
    Asset::Voxel::VoxelPosition normal,
    Vec3 modelCenter) noexcept;

[[nodiscard]] std::optional<UniversalCursor2DTarget>
MakeOutermostVoxelFaceCursor2DTarget(
    std::span<const Asset::Voxel::VoxelPosition> presentedPositions,
    Asset::Voxel::VoxelPosition lockedSeed,
    Asset::Voxel::VoxelPosition normal,
    Vec3 modelCenter) noexcept;

} // namespace VoxelForge::Editor
