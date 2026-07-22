#pragma once

#include "Constraints/ConstraintSettings.h"
#include "EditorMath.h"

namespace VoxelForge::Editor::Constraints
{

class GridConstraint final
{
public:
    [[nodiscard]] static Vec3 Apply(
        Vec3 position, GridConstraintStep step) noexcept;

private:
    [[nodiscard]] static float StepValue(
        GridConstraintStep step) noexcept;
};

} // namespace VoxelForge::Editor::Constraints
