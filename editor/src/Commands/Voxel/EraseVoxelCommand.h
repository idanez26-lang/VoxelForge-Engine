#pragma once

#include "Commands/EditorCommand.h"

#include "VoxelForge/Voxel/Voxel.h"

#include <cstdint>
#include <string_view>

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

class EraseVoxelCommand final : public EditorCommand
{
public:
    EraseVoxelCommand(
        VoxelEditSession& session,
        std::uint64_t modelGeneration,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z) noexcept;

    [[nodiscard]] CommandResult Execute() override;
    [[nodiscard]] CommandResult Undo() override;
    [[nodiscard]] CommandResult Redo() override;
    [[nodiscard]] std::string_view Name() const noexcept override;

    [[nodiscard]] const Voxel::Voxel& PreviousVoxel() const noexcept;

private:
    [[nodiscard]] CommandResult Apply(
        Voxel::Voxel expected,
        Voxel::Voxel replacement);
    [[nodiscard]] CommandResult ValidateSession(
        Voxel::VoxelModel*& model) const noexcept;

    VoxelEditSession& session_;
    std::uint64_t modelGeneration_ = 0U;
    std::uint32_t x_ = 0U;
    std::uint32_t y_ = 0U;
    std::uint32_t z_ = 0U;
    Voxel::Voxel previousVoxel_{};
    bool previousVoxelCaptured_ = false;
};

} // namespace VoxelForge::Editor
