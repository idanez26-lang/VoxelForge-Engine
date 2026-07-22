#pragma once

#include "Constraints/ConstraintSettings.h"
#include "BrushEngine/SmartBrushEngine.h"
#include "Transform/TransformPivotManager.h"

#include <array>
#include <cstdint>

namespace VoxelForge::Editor
{

struct PencilToolOptions final
{
    SmartBrushState State{};
    std::array<bool, 6U> Faces{{true, true, true, true, true, true}};
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
