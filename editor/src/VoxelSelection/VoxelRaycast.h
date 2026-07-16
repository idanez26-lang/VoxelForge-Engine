#pragma once

#include "VoxelRay.h"
#include "VoxelRayTransform.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace VoxelForge::Voxel
{
class VoxelGrid;
}

namespace VoxelForge::Editor
{

enum class VoxelHitFace : std::uint8_t
{
    None,
    NegativeX,
    PositiveX,
    NegativeY,
    PositiveY,
    NegativeZ,
    PositiveZ
};

struct VoxelCoordinates final
{
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Z = 0;

    [[nodiscard]] bool operator==(const VoxelCoordinates&) const noexcept = default;
};

struct VoxelRaycastHit final
{
    VoxelCoordinates Coordinates{};
    VoxelHitFace Face = VoxelHitFace::None;
    float Distance = 0.0F;
    Vec3 Impact{};
    std::uint8_t ColorIndex = 0;
    std::size_t SubModelIndex = 0U;
    Asset::Voxel::VoxelPosition AdjacentPosition{};
    bool AdjacentWithinBounds = false;
    Vec3 LocalPosition{};
    Vec3 WorldPosition{};
    Vec3 Normal{};

    [[nodiscard]] bool operator==(const VoxelRaycastHit&) const noexcept = default;
};

inline constexpr float VoxelRaycastEpsilon = 1.0e-5F;
inline constexpr float DefaultVoxelRaycastMaximumDistance = 10000.0F;
inline constexpr std::uint64_t MaximumVoxelRaycastSteps = 1000000U;

struct VoxelRaycastOptions final
{
    VoxelModelTransform Transform{};
    std::size_t SubModelIndex = 0U;
    float MaximumDistance = DefaultVoxelRaycastMaximumDistance;
    std::uint64_t MaximumSteps = MaximumVoxelRaycastSteps;
};

[[nodiscard]] std::optional<VoxelRaycastHit> RaycastVoxelGrid(
    const Voxel::VoxelGrid& grid,
    const VoxelRay& ray) noexcept;

[[nodiscard]] std::optional<VoxelRaycastHit> RaycastVoxelDocument(
    const Asset::Voxel::VoxelDocument& document,
    const VoxelRay& worldRay,
    const VoxelRaycastOptions& options = {}) noexcept;

[[nodiscard]] Asset::Voxel::VoxelPosition VoxelHitFaceIntegerNormal(
    VoxelHitFace face) noexcept;

[[nodiscard]] Vec3 VoxelHitFaceNormal(VoxelHitFace face) noexcept;

[[nodiscard]] const char* VoxelHitFaceName(VoxelHitFace face) noexcept;

} // namespace VoxelForge::Editor
