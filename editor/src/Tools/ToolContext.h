#pragma once

#include "Constraints/ConstraintSettings.h"
#include "SmartTools/SmartTool.h"
#include "Transform/TransformPivotManager.h"

#include <cstdint>

namespace VoxelForge::Editor
{

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
    SmartTool Smart{};
    ScaleToolOptions Scale{};
    ConstraintSettings* Constraints = nullptr;
    TransformPivotManager* PivotManager = nullptr;
};

} // namespace VoxelForge::Editor
