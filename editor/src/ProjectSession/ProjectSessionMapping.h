#pragma once

#include "EditorCamera.h"
#include "ProjectSession/ProjectSessionService.h"
#include "SmartTools/SmartTool.h"
#include "VoxelTools/VoxelToolState.h"

#include <cstddef>
#include <filesystem>

namespace VoxelForge::Editor
{

// Pure state mapping between the live editor tools and the persisted project
// session. No I/O, no logging, no service orchestration: EditorWorkspace owns
// validation, persistence and console feedback around these functions.

[[nodiscard]] ProjectSessionCamera ToSessionCamera(
    const EditorCameraState& camera) noexcept;

[[nodiscard]] EditorCameraState FromSessionCamera(
    const ProjectSessionCamera& camera) noexcept;

// Snapshots the live tool state into a serializable session. The relative
// model path is resolved and validated by the caller before being passed in.
[[nodiscard]] ProjectSessionData BuildSessionData(
    const EditorCameraState& camera,
    const VoxelToolState& toolState,
    const SmartTool& smart,
    std::size_t activePaletteIndex,
    std::filesystem::path lastModel);

// Applies the tool portion of a loaded session. Cube and Sphere were stored
// as SmartGeometry before Shape became the sole active geometry selector:
// the legacy brush volume is preserved while the live tool is normalized to
// Pencil for the current UI. Camera, palette and model restoration remain
// with the caller.
void ApplySessionToTools(
    const ProjectSessionData& session,
    SmartTool& smart,
    VoxelToolState& toolState) noexcept;

} // namespace VoxelForge::Editor
