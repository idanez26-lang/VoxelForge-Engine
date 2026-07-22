#pragma once

#include "Constraints/ConstraintSettings.h"
#include "Transform/TransformPivotManager.h"

#include <array>
#include <cstdint>

namespace VoxelForge::Editor
{

enum class PencilMode : std::uint8_t
{
    Add,
    Remove,
    Paint
};

enum class VoxelBrushShape : std::uint8_t
{
    Cube,
    Sphere
};

struct PencilToolOptions final
{
    PencilMode Mode = PencilMode::Add;
    std::array<bool, 6U> Faces{{true, true, true, true, true, true}};
    VoxelBrushShape Brush = VoxelBrushShape::Cube;
    int Size = 1;
};

struct ScaleToolOptions final
{
    bool Uniform = true;
    bool Snap = false;
};

// Live editor data supplied to the active tool panel. Constraint and pivot
// values remain owned by their existing services; the UI does not mirror them.
struct ToolContext final
{
    bool HasDocument = false;
    PencilToolOptions Pencil{};
    ScaleToolOptions Scale{};
    ConstraintSettings* Constraints = nullptr;
    TransformPivotManager* PivotManager = nullptr;
};

} // namespace VoxelForge::Editor
