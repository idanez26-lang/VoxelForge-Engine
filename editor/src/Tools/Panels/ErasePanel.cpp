#include "ErasePanel.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

void DrawErasePanel(ToolContext&)
{
    ImGui::TextDisabled("Mode");
    ImGui::TextUnformatted("Remove voxel");
    ImGui::Spacing();
    ImGui::TextDisabled("One click removes one voxel.");
}

} // namespace VoxelForge::Editor
