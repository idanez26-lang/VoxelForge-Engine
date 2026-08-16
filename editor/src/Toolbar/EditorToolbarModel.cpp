#include "EditorToolbarModel.h"

#include <algorithm>
#include <cmath>

namespace VoxelForge::Editor
{
namespace
{
// VF-UX-TOOLS : la barre expose desormais les FAMILLES. Les modes et les
// options restent dans le panneau juste en dessous, contextuels a la famille
// selectionnee. L'action (Add / Erase / Paint) est un axe independant : changer
// de famille ne la modifie pas.
constexpr std::array<EditorToolbarButton, EditorToolbarModel::ButtonCount>
    ToolbarButtons{{
        {EditorToolbarAction::Pencil, EditorToolbarGroup::Sculpt,
         "Pencil", "Freehand voxels with a 3D brush",
         EditorInputCommand::ToolFamilyPencil,
         ActiveVoxelTool::Pencil, true, SmartGeometry::Pencil},
        {EditorToolbarAction::Geometry, EditorToolbarGroup::Sculpt,
         "Geometry", "Draw 2D shapes: Line, Cube, Sphere",
         EditorInputCommand::ToolFamilyGeometry,
         ActiveVoxelTool::Pencil, true, SmartGeometry::Geometry},
        {EditorToolbarAction::Face, EditorToolbarGroup::Sculpt,
         "Face", "Act on a whole exposed face",
         EditorInputCommand::ToolFamilyFace,
         ActiveVoxelTool::Pencil, true, SmartGeometry::Face},
        {EditorToolbarAction::Surface, EditorToolbarGroup::Sculpt,
         "Surface", "Act on a connected surface",
         EditorInputCommand::ToolFamilySurface,
         ActiveVoxelTool::Pencil, true, SmartGeometry::Surface},
        {EditorToolbarAction::Fill, EditorToolbarGroup::Sculpt,
         "Fill", "Fill a connected region or a face plane",
         EditorInputCommand::ToolFamilyFill,
         ActiveVoxelTool::Pencil, true, SmartGeometry::Fill},
        {EditorToolbarAction::Selection, EditorToolbarGroup::Selection,
         "Selection", "Select voxels", EditorInputCommand::ToolSelection,
         ActiveVoxelTool::Selection},
        {EditorToolbarAction::Transform, EditorToolbarGroup::Transform,
         "Transform", "Move the selected voxels", EditorInputCommand::ToolMove,
         ActiveVoxelTool::Move}
    }};
}

std::span<const EditorToolbarButton, EditorToolbarModel::PrimaryButtonCount>
EditorToolbarModel::PrimaryButtons() noexcept
{
    return std::span<const EditorToolbarButton, PrimaryButtonCount>(
        ToolbarButtons.data(), PrimaryButtonCount);
}

const std::array<EditorToolbarButton, EditorToolbarModel::ButtonCount>&
EditorToolbarModel::Buttons() noexcept
{
    return ToolbarButtons;
}

bool EditorToolbarModel::IsEnabled(
    const EditorToolbarButton& button,
    const EditorToolbarState& state) noexcept
{
    if (button.Action == EditorToolbarAction::Transform)
        return state.HasDocument && state.CanMoveSelection;
    if (button.Action == EditorToolbarAction::Duplicate)
        return state.HasDocument && state.CanDuplicateSelection;
    if (button.Action == EditorToolbarAction::Rotate)
        return state.HasDocument && state.CanRotateSelection;
    if (button.Action == EditorToolbarAction::Scale)
        return state.HasDocument && state.CanScaleSelection;
    if (button.Action == EditorToolbarAction::Mirror)
        return state.HasDocument && state.CanMirrorSelection;
    if (button.Action == EditorToolbarAction::Align)
        return state.HasDocument && state.CanAlignSelection;
    return state.HasDocument;
}

bool EditorToolbarModel::IsActive(
    const EditorToolbarButton& button,
    const EditorToolbarState& state) noexcept
{
    if (!state.HasDocument) return false;
    // VF-UX-TOOLS : un bouton de famille n'est actif que si l'outil Smart est
    // selectionne ET que la geometrie courante est la sienne. Sans le second
    // test, les cinq familles s'allumeraient simultanement.
    if (button.HasFamily)
    {
        if (state.ActiveTool != ActiveVoxelTool::Pencil) return false;
        // Une seule autorite repond a « quelle famille ? ». La barre recopiait
        // la regle Geometry/Line ; elle la demande desormais.
        return FamilyOf(state.ActiveGeometry) == FamilyOf(button.Family);
    }
    if (button.Action == EditorToolbarAction::Transform)
        return state.ActiveTool == ActiveVoxelTool::Move ||
            state.ActiveTool == ActiveVoxelTool::Duplicate ||
            state.ActiveTool == ActiveVoxelTool::Rotate ||
            state.ActiveTool == ActiveVoxelTool::Mirror ||
            state.ActiveTool == ActiveVoxelTool::Scale ||
            state.ActiveTool == ActiveVoxelTool::Align ||
            state.ActiveTool == ActiveVoxelTool::Wrap;
    return button.Tool != ActiveVoxelTool::None &&
        button.Tool == state.ActiveTool;
}

EditorToolbarLayout EditorToolbarModel::CalculateLayout(
    const float availableWidth,
    const float fontSize) noexcept
{
    const float safeWidth = std::max(availableWidth, 1.0F);
    const float safeFontSize = std::max(fontSize, 1.0F);
    const float preferredButton = std::clamp(
        std::ceil(safeFontSize * 2.05F), 32.0F, 40.0F);
    const float regularSpacing = std::clamp(
        std::floor(safeFontSize * 0.32F), 4.0F, 7.0F);
    const float groupSpacing = std::clamp(
        std::floor(safeFontSize * 0.75F), 10.0F, 16.0F);
    const float singleRowWidth = preferredButton * PrimaryButtonCount +
        regularSpacing * static_cast<float>(PrimaryButtonCount - 1U) +
        groupSpacing * 2.0F;
    if (singleRowWidth <= safeWidth)
        return {preferredButton, regularSpacing, groupSpacing, false};

    constexpr float MinimumButtonSize = 24.0F;
    // Le diviseur suivait autrefois les trois boutons d'origine ; il doit
    // suivre le nombre reel, sinon la rangee repliee deborde.
    constexpr float ButtonsPerWrappedRow =
        static_cast<float>(EditorToolbarModel::PrimaryButtonCount);
    const float wrappedButton = std::clamp(
        (safeWidth - regularSpacing * (ButtonsPerWrappedRow - 1.0F)) /
            ButtonsPerWrappedRow,
        MinimumButtonSize, preferredButton);
    return {wrappedButton, regularSpacing, groupSpacing, true};
}

} // namespace VoxelForge::Editor
