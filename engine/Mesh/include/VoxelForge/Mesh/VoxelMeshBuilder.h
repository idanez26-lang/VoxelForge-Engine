#pragma once

#include "MeshData.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <optional>
#include <string>

namespace VoxelForge::Mesh
{

enum class MeshBuildError
{
    None,
    InvalidSource,
    TooLarge,
    AllocationFailure,
    InconsistentResult
};

struct MeshBuildResult final
{
    bool Succeeded = false;
    MeshBuildError Error = MeshBuildError::None;
    std::string Message;
    std::optional<MeshData> Mesh;
};

class VoxelMeshBuilder final
{
public:
    static constexpr std::size_t MaximumFaceCount = 256U * 1024U;
    static constexpr std::size_t MaximumVertexCount = MaximumFaceCount * 4U;
    static constexpr std::size_t MaximumIndexCount = MaximumFaceCount * 6U;

    [[nodiscard]] static MeshBuildResult Build(
        const Voxel::VoxelGrid& grid);
    [[nodiscard]] static MeshBuildResult Build(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t modelIndex = 0U);
    // VF-0262 (lot 262-1): builds only the faces of the voxels lying inside
    // the inclusive region [regionMinimum, regionMaximum]. Face visibility
    // still consults the whole sub-model, so meshes built over a partition of
    // space assemble seamlessly: their union carries exactly the faces of the
    // full document mesh. The face limits apply per call.
    [[nodiscard]] static MeshBuildResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const Asset::Voxel::VoxelPosition& regionMinimum,
        const Asset::Voxel::VoxelPosition& regionMaximum,
        std::size_t modelIndex = 0U);

private:
    // Shared document construction; a null region builds the whole sub-model.
    [[nodiscard]] static MeshBuildResult BuildDocumentMesh(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t modelIndex,
        const Asset::Voxel::VoxelPosition* regionMinimum,
        const Asset::Voxel::VoxelPosition* regionMaximum);
};

} // namespace VoxelForge::Mesh
