#pragma once

#include "Constraints/ConstraintSettings.h"
#include "BrushEngine/SmartBrushEngine.h"
#include "Transform/TransformPivotManager.h"
#include "VoxelTools/VoxelPaintBrushTool.h"

#include <array>
#include <cstdint>
#include <optional>

namespace VoxelForge::Editor
{

struct PencilToolOptions final
{
    SmartBrushMode Mode = SmartBrushMode::Add;
    std::array<bool, 6U> Faces{{true, true, true, true, true, true}};
    std::optional<SmartBrushStatistics> Statistics;
};

struct PaintToolOptions final
{
    std::optional<VoxelPaintBrushStatistics> Statistics;
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
    // Shape, dimension, orientation and size persist when switching between
    // Pencil and Paint. Each service receives its own local mode copy.
    SmartBrushState Brush{};
    PencilToolOptions Pencil{};
    PaintToolOptions Paint{};
    ScaleToolOptions Scale{};
    ConstraintSettings* Constraints = nullptr;
    TransformPivotManager* PivotManager = nullptr;
};

} // namespace VoxelForge::Editor
