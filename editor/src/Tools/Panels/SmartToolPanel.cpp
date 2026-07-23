#include "SmartBrushOptions.h"
#include "SmartToolPanel.h"

#include <imgui.h>

namespace VoxelForge::Editor
{
bool DrawSmartToolPanel(ToolContext& context)
{
    SmartTool& tool = context.Smart;
    bool changed = false;
    if (tool.Geometry() == SmartGeometry::Cube ||
        tool.Geometry() == SmartGeometry::Sphere)
    {
        tool.Brush().Shape = ResolveSmartBrushShape(
            tool.Geometry(), tool.Brush().Shape);
        tool.SetGeometry(SmartGeometry::Pencil);
        changed = true;
    }
    ImGui::TextDisabled("SMART TOOL");
    ImGui::TextDisabled("Geometry");
    ImGui::PushID("Geometry");
    int geometry = static_cast<int>(SmartGeometry::Pencil);
    changed |= ImGui::RadioButton(
        "Pencil", &geometry, static_cast<int>(SmartGeometry::Pencil));
    ImGui::BeginDisabled();
    ImGui::RadioButton("Face", &geometry, static_cast<int>(SmartGeometry::Face));
    ImGui::RadioButton("Box", &geometry, static_cast<int>(SmartGeometry::Box));
    ImGui::RadioButton("Line", &geometry, static_cast<int>(SmartGeometry::Line));
    ImGui::EndDisabled();
    ImGui::PopID();
    const SmartGeometry geometryBefore = tool.Geometry();
    tool.SetGeometry(static_cast<SmartGeometry>(geometry));
    changed |= tool.Geometry() != geometryBefore;

    ImGui::TextDisabled("Action");
    ImGui::PushID("Action");
    int action = static_cast<int>(tool.Action());
    changed |= ImGui::RadioButton(
        "Add", &action, static_cast<int>(SmartAction::Add));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Paint", &action, static_cast<int>(SmartAction::Paint));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Erase", &action, static_cast<int>(SmartAction::Erase));
    ImGui::BeginDisabled();
    ImGui::SameLine();
    ImGui::RadioButton(
        "Replace", &action, static_cast<int>(SmartAction::Replace));
    ImGui::EndDisabled();
    ImGui::PopID();
    const SmartAction actionBefore = tool.Action();
    tool.SetAction(static_cast<SmartAction>(action));
    changed |= tool.Action() != actionBefore;

    changed |= DrawSmartBrushOptions(tool.Brush());
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
    return changed;
}
} // namespace VoxelForge::Editor
