#include "ScalePanel.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

void DrawScalePanel(ToolContext& context)
{
    ImGui::Checkbox("Uniform", &context.Scale.Uniform);
    ImGui::BeginDisabled();
    ImGui::Checkbox("Snap", &context.Scale.Snap);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Scale snapping is prepared for a future release.");
}

} // namespace VoxelForge::Editor
