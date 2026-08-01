#pragma once

// Noms des fenêtres/panneaux de l'atelier — source de vérité partagée.
// (VF-0260, lot 2. Les copies locales des unités EditorWorkspace*.cpp
// seront résorbées progressivement.)
namespace VoxelForge::Editor::PanelNames
{

inline constexpr float StatusBarHeight = 26.0F;
inline constexpr const char* WorkspaceDockspace = "VoxelForgeStudioDockSpace";
inline constexpr const char* Tools = "Tools";
inline constexpr const char* ToolOptions = "Tool Options";
inline constexpr const char* Style = "Style";
inline constexpr const char* Viewport = "Viewport";
inline constexpr const char* Assets = "Assets";
inline constexpr const char* ForgeLibrary = "Forge Library";
inline constexpr const char* Scene = "Scene";
inline constexpr const char* Inspector = "Inspector";
inline constexpr const char* Transform = "Transform";
inline constexpr const char* Console = "Console";

} // namespace VoxelForge::Editor::PanelNames
