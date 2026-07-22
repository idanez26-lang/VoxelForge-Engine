#pragma once

#include <cstdint>

namespace VoxelForge::Editor
{

enum class GridConstraintStep : std::uint8_t
{
    Quarter,
    Half,
    One,
    Two,
    Four,
    Eight,
    Sixteen,
    ThirtyTwo,
    SixtyFour
};

enum class RotationConstraintStep : std::uint8_t
{
    Degrees5,
    Degrees10,
    Degrees15,
    Degrees30,
    Degrees45,
    Degrees90
};

// Runtime policy supplied to ConstraintEngine. Future constraint families can
// extend this value type without changing the Solve() entry point.
struct ConstraintSettings final
{
    bool GridEnabled = false;
    GridConstraintStep GridStep = GridConstraintStep::One;
    bool RotationEnabled = false;
    RotationConstraintStep RotationStep =
        RotationConstraintStep::Degrees90;
};

} // namespace VoxelForge::Editor
