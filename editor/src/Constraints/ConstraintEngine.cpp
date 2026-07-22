#include "ConstraintEngine.h"

#include "Constraints/GridConstraint.h"
#include "Constraints/RotationConstraint.h"

namespace VoxelForge::Editor
{

ConstraintResult ConstraintEngine::Solve(
    const ConstraintRequest& request) noexcept
{
    ConstraintResult result;
    result.Transform = request.Transform;

    if (request.Settings.GridEnabled)
    {
        result.Transform.Position = Constraints::GridConstraint::Apply(
            request.Transform.Position, request.Settings.GridStep);
        result.PositionChanged =
            result.Transform.Position != request.Transform.Position;
    }
    if (request.Settings.RotationEnabled)
    {
        result.Transform.RotationDegrees =
            Constraints::RotationConstraint::Apply(
                request.Transform.RotationDegrees,
                request.Settings.RotationStep);
        result.RotationChanged = result.Transform.RotationDegrees !=
            request.Transform.RotationDegrees;
    }
    return result;
}

} // namespace VoxelForge::Editor
