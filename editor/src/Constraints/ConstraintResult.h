#pragma once

#include "ConstraintRequest.h"

namespace VoxelForge::Editor
{

struct ConstraintResult final
{
    ConstraintTransform Transform{};
    bool PositionChanged = false;
    bool RotationChanged = false;

    [[nodiscard]] bool Changed() const noexcept
    {
        return PositionChanged || RotationChanged;
    }
};

} // namespace VoxelForge::Editor
