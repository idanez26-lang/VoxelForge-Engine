#pragma once

#include "Constraints/ConstraintSettings.h"
#include "SmartTools/SmartTool.h"
#include "VoxelTools/VoxelToolState.h"
#include "Selection/SelectionUxModes.h"
#include "Transform/WrapVoxelSelectionOperation.h"
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
    // VF-UX-SELECTION-V1 : options de la famille Selection. Le panneau les
    // edite, le viewport les lit — aucune duplication d'etat.
    SelectionToolOptions Selection{};
    // VF-WRAP-V1 : spacing et mirror-repeat de Wrap. Les ancres par axe sont
    // posees par le geste (face tiree), jamais par le panneau.
    VoxelWrapOptions Wrap{};
    // VF-UX (validation Tony) : la rangee de modes Transform du panneau doit
    // pouvoir changer d'outil par le MEME chemin que la barre. Le panneau ne
    // porte aucune logique metier : il demande, le workspace execute.
    std::function<void(ActiveVoxelTool)> SelectTool;
    ConstraintSettings* Constraints = nullptr;
    TransformPivotManager* PivotManager = nullptr;
};

} // namespace VoxelForge::Editor
