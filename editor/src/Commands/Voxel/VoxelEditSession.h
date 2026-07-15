#pragma once

#include "Commands/EditorCommand.h"

#include <cstdint>

namespace VoxelForge::Voxel
{
class VoxelModel;
}

namespace VoxelForge::Editor
{

class VoxelEditSession
{
public:
    virtual ~VoxelEditSession() = default;

    [[nodiscard]] virtual std::uint64_t VoxelModelGeneration() const noexcept = 0;
    [[nodiscard]] virtual Voxel::VoxelModel* ActiveVoxelModel() noexcept = 0;
    [[nodiscard]] virtual CommandResult RebuildActiveVoxelMesh() = 0;
    virtual void CompleteVoxelEdit() noexcept = 0;
};

} // namespace VoxelForge::Editor
