#pragma once

// VF-WRAP-V1 (correction visuelle) — autorite PURE des poignees de faces des
// bornes editables : quels outils les MONTRENT (guides du volume edite) et
// quels outils les SAISISSENT (survol + capture au clic).
//
// Avant cette autorite, la garde vivait en dur dans le viewport et
// n'incluait pas Wrap : aucune poignee n'etait projetee, dessinee ni piquee
// pour Wrap, alors que le backend BeginWrapHandleDrag / UpdateWrapHandleDrag /
// ReleaseWrapHandleDrag existait. Selection redimensionne les faces ; Wrap les
// tire pour Repeat / Crop — le meme systeme visuel et de picking, un backend
// different, jamais une seconde interaction parallele.

#include "VoxelTools/VoxelToolState.h"

namespace VoxelForge::Editor
{

// Outils pour lesquels les six poignees de faces des bornes editables sont
// projetees et dessinees (a titre de guides ou de commandes).
[[nodiscard]] constexpr bool ToolShowsSelectionHandles(
    const ActiveVoxelTool tool) noexcept
{
    switch (tool)
    {
    case ActiveVoxelTool::Selection:
    case ActiveVoxelTool::Move:
    case ActiveVoxelTool::Duplicate:
    case ActiveVoxelTool::Rotate:
    case ActiveVoxelTool::Mirror:
    case ActiveVoxelTool::Scale:
    case ActiveVoxelTool::Align:
    case ActiveVoxelTool::Wrap:
        return true;
    default:
        return false;
    }
}

// Outils dont les poignees se SURVOLENT et se SAISISSENT : Selection
// (redimensionnement des bornes) et Wrap (Repeat / Crop par la face tiree).
// Les autres Transform pilotent leur gizmo ; leurs poignees restent des guides.
[[nodiscard]] constexpr bool ToolPicksSelectionHandles(
    const ActiveVoxelTool tool) noexcept
{
    return tool == ActiveVoxelTool::Selection || tool == ActiveVoxelTool::Wrap;
}

} // namespace VoxelForge::Editor
