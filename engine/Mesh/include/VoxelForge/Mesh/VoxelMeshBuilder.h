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
};

} // namespace VoxelForge::Mesh
