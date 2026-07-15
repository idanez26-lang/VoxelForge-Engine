#pragma once

#include "VoxelGrid.h"
#include "VoxelPalette.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Voxel
{

class VoxelModel final
{
public:
    // Value semantics are intentional: models can be copied independently or moved.
    [[nodiscard]] const std::string& Name() const noexcept;
    void SetName(std::string_view name);

    [[nodiscard]] VoxelPalette& Palette() noexcept;
    [[nodiscard]] const VoxelPalette& Palette() const noexcept;
    [[nodiscard]] const std::vector<VoxelGrid>& Grids() const noexcept;
    // Grid pointers are invalidated when grids are added or removed.
    [[nodiscard]] VoxelGrid* GetGrid(std::size_t index) noexcept;
    [[nodiscard]] const VoxelGrid* GetGrid(std::size_t index) const noexcept;

    void AddGrid(VoxelGrid grid);
    [[nodiscard]] bool RemoveGrid(std::size_t index);
    [[nodiscard]] std::size_t GridCount() const noexcept;
    // Restores the complete default model: empty name, palette and grid list.
    void Clear() noexcept;
    [[nodiscard]] std::uint64_t TotalVoxelCount() const noexcept;
    [[nodiscard]] std::uint64_t TotalOccupiedVoxelCount() const noexcept;

private:
    std::string name_;
    VoxelPalette palette_;
    std::vector<VoxelGrid> grids_;
};

} // namespace VoxelForge::Voxel
