#pragma once

#include "SelectionService.h"
#include "VoxelSelection/VoxelRay.h"

#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

struct SelectionBoxRayHit final
{
    Vec3 WorldPosition{};
    float EntryDistance = 0.0F;
    float ExitDistance = 0.0F;
};

struct SelectionMovePlane final
{
    Vec3 Point{};
    Vec3 Normal{};
    bool Valid = false;
};

enum class SelectionPointerTarget : std::uint8_t
{
    Exterior,
    Interior,
    Handle
};

[[nodiscard]] std::optional<SelectionBoxRayHit>
    PickSelectionBoxInterior(
        const SelectionBounds& bounds,
        Vec3 modelCenter,
        const VoxelRay& ray,
        std::optional<float> occluderDistance = std::nullopt) noexcept;

[[nodiscard]] SelectionMovePlane MakeSelectionMovePlane(
    Vec3 anchorWorldPosition,
    Vec3 viewDirection) noexcept;

[[nodiscard]] std::optional<Vec3> IntersectSelectionMovePlane(
    const VoxelRay& ray,
    const SelectionMovePlane& plane) noexcept;

[[nodiscard]] SelectionBounds TranslateSelectionBounds(
    const SelectionBounds& original,
    Asset::Voxel::VoxelPosition requestedDelta,
    Asset::Voxel::VoxelDimensions documentDimensions) noexcept;

[[nodiscard]] SelectionPointerTarget ResolveSelectionPointerTarget(
    bool handleHovered,
    bool interiorHovered) noexcept;

} // namespace VoxelForge::Editor
