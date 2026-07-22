#include "MovePanel.h"

#include <imgui.h>

#include <iterator>

namespace VoxelForge::Editor
{
namespace
{
constexpr const char* GridStepNames[] = {
    "0.25", "0.5", "1", "2", "4", "8", "16", "32", "64"};
}

void DrawMovePanel(ToolContext& context)
{
    if (context.Constraints == nullptr)
    {
        ImGui::TextDisabled("Constraint settings unavailable.");
        return;
    }
    ConstraintSettings& settings = *context.Constraints;
    ImGui::Checkbox("Grid Snap", &settings.GridEnabled);
    int step = static_cast<int>(settings.GridStep);
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::Combo("Step", &step, GridStepNames,
            static_cast<int>(std::size(GridStepNames))))
        settings.GridStep = static_cast<GridConstraintStep>(step);

    ImGui::TextDisabled("Axis");
    ImGui::BeginDisabled();
    bool enabled = true;
    ImGui::Checkbox("X", &enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Y", &enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Z", &enabled);
    ImGui::EndDisabled();
}

} // namespace VoxelForge::Editor
