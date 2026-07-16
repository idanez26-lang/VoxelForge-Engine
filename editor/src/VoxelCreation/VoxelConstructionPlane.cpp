#include "VoxelConstructionPlane.h"
#include "VoxelCreation/WorkplaneService.h"

namespace VoxelForge::Editor
{

std::optional<Asset::Voxel::VoxelPosition>
FindVoxelConstructionPlaneTarget(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t subModelIndex,
    const VoxelRay& worldRay,
    const Vec3 modelCenter) noexcept
{
    const WorkplaneHit hit = WorkplaneService{}.Intersect(
        document, subModelIndex, worldRay, modelCenter);
    return hit.IsValid() ? hit.Position : std::nullopt;
}

} // namespace VoxelForge::Editor
