#include "RotationConstraint.h"

#include <cmath>

namespace VoxelForge::Editor::Constraints
{
namespace
{
[[nodiscard]] float Snap(const float value, const float step) noexcept
{
    if (!std::isfinite(value) || !std::isfinite(step) || step <= 0.0F)
        return value;
    const double snapped = std::round(
        static_cast<double>(value) / static_cast<double>(step)) *
        static_cast<double>(step);
    return snapped == 0.0 ? 0.0F : static_cast<float>(snapped);
}
}

Vec3 RotationConstraint::Apply(
    const Vec3 rotationDegrees,
    const RotationConstraintStep step) noexcept
{
    const float value = StepValue(step);
    return {
        Snap(rotationDegrees.X, value),
        Snap(rotationDegrees.Y, value),
        Snap(rotationDegrees.Z, value)};
}

float RotationConstraint::StepValue(
    const RotationConstraintStep step) noexcept
{
    switch (step)
    {
    case RotationConstraintStep::Degrees5: return 5.0F;
    case RotationConstraintStep::Degrees10: return 10.0F;
    case RotationConstraintStep::Degrees15: return 15.0F;
    case RotationConstraintStep::Degrees30: return 30.0F;
    case RotationConstraintStep::Degrees45: return 45.0F;
    case RotationConstraintStep::Degrees90: return 90.0F;
    }
    return 90.0F;
}

} // namespace VoxelForge::Editor::Constraints
