#pragma once

#include "Commands/EditorCommand.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Voxel/Voxel.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace VoxelForge::Editor
{

class PaintVoxelCommand final : public EditorCommand
{
public:
    PaintVoxelCommand(
        VoxelEditSession& session,
        std::uint64_t modelGeneration,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z,
        std::size_t newColorIndex) noexcept;

    [[nodiscard]] CommandResult Execute() override;
    [[nodiscard]] CommandResult Undo() override;
    [[nodiscard]] CommandResult Redo() override;
    [[nodiscard]] std::string_view Name() const noexcept override;

    [[nodiscard]] const Voxel::Voxel& PreviousVoxel() const noexcept;
    [[nodiscard]] const Voxel::Voxel& PaintedVoxel() const noexcept;

private:
    [[nodiscard]] CommandResult Apply(
        Voxel::Voxel expected,
        Voxel::Voxel replacement);

    VoxelEditSession& session_;
    std::uint64_t modelGeneration_ = 0U;
    std::uint32_t x_ = 0U;
    std::uint32_t y_ = 0U;
    std::uint32_t z_ = 0U;
    std::size_t newColorIndex_ = 0U;
    Voxel::Voxel previousVoxel_{};
    Voxel::Voxel paintedVoxel_{};
    bool previousVoxelCaptured_ = false;
};

} // namespace VoxelForge::Editor
