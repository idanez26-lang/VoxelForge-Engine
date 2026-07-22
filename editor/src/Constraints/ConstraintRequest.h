#pragma once

#include "ConstraintSettings.h"
#include "EditorMath.h"

namespace VoxelForge::Editor
{

// Tool-neutral transform snapshot. Scale is carried through untouched in V1
// so callers can keep using one request shape as new constraints are added.
struct ConstraintTransform final
{
    Vec3 Position{};
    Vec3 RotationDegrees{};
    Vec3 Scale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const ConstraintTransform&) const noexcept =
        default;
};

struct ConstraintRequest final
{
    ConstraintTransform Transform{};
    ConstraintSettings Settings{};
};

} // namespace VoxelForge::Editor
