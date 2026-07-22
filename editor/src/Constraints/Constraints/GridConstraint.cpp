#include "GridConstraint.h"

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

Vec3 GridConstraint::Apply(
    const Vec3 position, const GridConstraintStep step) noexcept
{
    const float value = StepValue(step);
    return {
        Snap(position.X, value),
        Snap(position.Y, value),
        Snap(position.Z, value)};
}

float GridConstraint::StepValue(const GridConstraintStep step) noexcept
{
    switch (step)
    {
    case GridConstraintStep::Quarter: return 0.25F;
    case GridConstraintStep::Half: return 0.5F;
    case GridConstraintStep::One: return 1.0F;
    case GridConstraintStep::Two: return 2.0F;
    case GridConstraintStep::Four: return 4.0F;
    case GridConstraintStep::Eight: return 8.0F;
    case GridConstraintStep::Sixteen: return 16.0F;
    case GridConstraintStep::ThirtyTwo: return 32.0F;
    case GridConstraintStep::SixtyFour: return 64.0F;
    }
    return 1.0F;
}

} // namespace VoxelForge::Editor::Constraints
