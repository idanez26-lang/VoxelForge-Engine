#pragma once

#include "Constraints/ConstraintSettings.h"
#include "EditorMath.h"

namespace VoxelForge::Editor::Constraints
{

class RotationConstraint final
{
public:
    [[nodiscard]] static Vec3 Apply(
        Vec3 rotationDegrees, RotationConstraintStep step) noexcept;

private:
    [[nodiscard]] static float StepValue(
        RotationConstraintStep step) noexcept;
};

} // namespace VoxelForge::Editor::Constraints
