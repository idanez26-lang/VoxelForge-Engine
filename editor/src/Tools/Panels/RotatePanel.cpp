#include "RotatePanel.h"

#include <imgui.h>

#include <iterator>

namespace VoxelForge::Editor
{
namespace
{
constexpr const char* AngleNames[] = {"5", "10", "15", "30", "45", "90"};
constexpr const char* PivotNames[] = {"Center", "Bottom", "Top"};
}

void DrawRotatePanel(ToolContext& context)
{
    if (context.Constraints != nullptr)
    {
        ConstraintSettings& settings = *context.Constraints;
        ImGui::Checkbox("Angle Snap", &settings.RotationEnabled);
        int angle = static_cast<int>(settings.RotationStep);
        ImGui::SetNextItemWidth(110.0F);
        if (ImGui::Combo("Angle", &angle, AngleNames,
                static_cast<int>(std::size(AngleNames))))
            settings.RotationStep = static_cast<RotationConstraintStep>(angle);
    }

    if (context.PivotManager != nullptr)
    {
        int pivot = static_cast<int>(context.PivotManager->GetMode());
        ImGui::SetNextItemWidth(110.0F);
        if (ImGui::Combo("Pivot", &pivot, PivotNames,
                static_cast<int>(std::size(PivotNames))))
            static_cast<void>(context.PivotManager->SetMode(
                static_cast<TransformPivotMode>(pivot)));
    }
}

} // namespace VoxelForge::Editor
