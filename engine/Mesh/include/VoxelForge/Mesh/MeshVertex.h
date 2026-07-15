#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace VoxelForge::Mesh
{

struct MeshVertex final
{
    std::array<float, 3> Position{};
    std::array<float, 3> Normal{};
    // Palette indices keep geometry compact and allow palette edits without
    // rebuilding four RGBA values per face vertex.
    std::uint8_t ColorIndex = 0;
    // Explicitly initialized tail padding keeps the 4-byte layout deterministic.
    std::array<std::uint8_t, 3> Padding{};

    [[nodiscard]] bool operator==(const MeshVertex&) const noexcept = default;
};

static_assert(sizeof(MeshVertex) == 28U);
static_assert(alignof(MeshVertex) == alignof(float));
static_assert(std::is_trivially_copyable_v<MeshVertex>);

} // namespace VoxelForge::Mesh
