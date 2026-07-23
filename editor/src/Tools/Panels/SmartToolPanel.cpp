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
    ImGui::BeginDisabled();
    ImGui::RadioButton("Erase", &action, static_cast<int>(SmartAction::Erase));
    ImGui::SameLine();
    ImGui::RadioButton(
        "Replace", &action, static_cast<int>(SmartAction::Replace));
    ImGui::EndDisabled();
    tool.SetAction(static_cast<SmartAction>(action));

    DrawSmartBrushOptions(tool.Brush());
    ImGui::TextDisabled("Advanced");
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("More controls coming soon");
    ImGui::EndDisabled();

    ImGui::TextDisabled("Statistics");
    if (tool.Statistics().Available)
    {
        const auto& stats = tool.Statistics();
        DrawSmartBrushStatistics(
            tool.Action() == SmartAction::Paint ? "Painted" : "New",
            stats.Changed,
            tool.Action() == SmartAction::Paint ? "Ignored" : "Existing",
            stats.Unchanged,
            stats.Total,
            stats.Clipped);
    }
}
} // namespace VoxelForge::Editor
