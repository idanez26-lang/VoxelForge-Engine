#include "PaintPanel.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

void DrawPaintPanel(ToolContext&)
{
    ImGui::TextDisabled("Mode");
    ImGui::TextUnformatted("Connected color");
    ImGui::Spacing();
    ImGui::TextDisabled("Uses the active Palette color.");
}

} // namespace VoxelForge::Editor
