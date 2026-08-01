#pragma once

using ImGuiID = unsigned int;

namespace VoxelForge::Editor
{

// Géométrie du dockspace de l'atelier (VF-0260, lot 2 — extrait
// d'EditorWorkspace). Fonctions pures ImGui : elles construisent les nœuds
// de dock et y rangent les fenêtres par nom, sans toucher à l'état de
// l'éditeur. La visibilité des panneaux reste la responsabilité de
// l'appelant (EditorWorkspace::Apply*PanelVisibility).
void DrawEditorDockSpace(ImGuiID dockspaceId);
void BuildDefaultDockLayout(ImGuiID dockspaceId);
void BuildThumbnailVisualDockLayout(ImGuiID dockspaceId);

} // namespace VoxelForge::Editor
