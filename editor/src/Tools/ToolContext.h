#pragma once

#include "Constraints/ConstraintSettings.h"
#include "SmartTools/SmartTool.h"
#include "Transform/TransformPivotManager.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>

namespace VoxelForge::Editor
{
class BrushProfileService;

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
    BrushProfileService* BrushProfiles = nullptr;
    std::function<std::optional<std::size_t>()> ActivePaletteIndex;
    std::function<bool(std::size_t)> SelectPaletteIndex;
    // Presentation-only query; the interaction phase remains owned by the
    // workspace and is never mirrored into Smart Tool business state.
    std::function<bool()> IsGeometryCylinderHeightPhase;
    ScaleToolOptions Scale{};
    ConstraintSettings* Constraints = nullptr;
    TransformPivotManager* PivotManager = nullptr;
};

} // namespace VoxelForge::Editor
