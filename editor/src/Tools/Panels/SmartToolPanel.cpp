#include "SmartBrushOptions.h"
#include "SmartToolPanel.h"

#include <imgui.h>

namespace VoxelForge::Editor
{
void DrawSmartToolPanel(ToolContext& context)
{
    SmartTool& tool = context.Smart;
    ImGui::TextDisabled("SMART TOOL");
    ImGui::TextDisabled("Geometry");
    int geometry = static_cast<int>(tool.Geometry());
    ImGui::RadioButton(
        "Pencil", &geometry, static_cast<int>(SmartGeometry::Pencil));
    ImGui::SameLine();
    ImGui::RadioButton("Cube", &geometry, static_cast<int>(SmartGeometry::Cube));
    ImGui::SameLine();
    ImGui::RadioButton("Sphere", &geometry,
        static_cast<int>(SmartGeometry::Sphere));
    ImGui::BeginDisabled();
    ImGui::RadioButton("Face", &geometry, static_cast<int>(SmartGeometry::Face));
    ImGui::RadioButton("Box", &geometry, static_cast<int>(SmartGeometry::Box));
    ImGui::RadioButton("Line", &geometry, static_cast<int>(SmartGeometry::Line));
    ImGui::EndDisabled();
    tool.SetGeometry(static_cast<SmartGeometry>(geometry));

    ImGui::TextDisabled("Action");
    int action = static_cast<int>(tool.Action());
    ImGui::RadioButton("Add", &action, static_cast<int>(SmartAction::Add));
    ImGui::SameLine();
    ImGui::RadioButton("Paint", &action, static_cast<int>(SmartAction::Paint));
    ImGui::SameLine();
    ImGui::RadioButton("Erase", &action, static_cast<int>(SmartAction::Erase));
    ImGui::BeginDisabled();
    ImGui::SameLine();
    ImGui::RadioButton(
        "Replace", &action, static_cast<int>(SmartAction::Replace));
    ImGui::EndDisabled();
    tool.SetAction(static_cast<SmartAction>(action));

    DrawSmartBrushOptions(tool.Brush(),
        tool.Geometry() == SmartGeometry::Pencil);
    ImGui::TextDisabled("Advanced");
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("More controls coming soon");
    ImGui::EndDisabled();

    ImGui::TextDisabled("Statistics");
    if (tool.Statistics().Available)
    {
        const auto& stats = tool.Statistics();
        const bool erasing = tool.Action() == SmartAction::Erase;
        const bool painting = tool.Action() == SmartAction::Paint;
        DrawSmartBrushStatistics(
            erasing ? "Erased" : painting ? "Painted" : "New",
            stats.Changed,
            erasing || painting ? "Ignored" : "Existing",
            stats.Unchanged,
            stats.Total,
            stats.Clipped);
    }
}
} // namespace VoxelForge::Editor
