#include "PencilPanel.h"
#include "SmartBrushOptions.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

void DrawPencilPanel(ToolContext& context)
{
    ImGui::TextDisabled("Mode");
    int mode = static_cast<int>(context.Pencil.Mode);
    ImGui::RadioButton("Add", &mode, static_cast<int>(SmartBrushMode::Add));
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::RadioButton("Remove", &mode, static_cast<int>(SmartBrushMode::Erase));
    ImGui::SameLine();
    ImGui::RadioButton("Paint", &mode, static_cast<int>(SmartBrushMode::Paint));
    ImGui::EndDisabled();
    context.Pencil.Mode = static_cast<SmartBrushMode>(mode);

    ImGui::TextDisabled("Faces");
    constexpr const char* FaceNames[] = {
        "Top", "Bottom", "Left", "Right", "Front", "Back"};
    ImGui::BeginDisabled();
    for (std::size_t index = 0U; index < context.Pencil.Faces.size(); ++index)
    {
        if ((index % 3U) != 0U) ImGui::SameLine();
        ImGui::Checkbox(FaceNames[index], &context.Pencil.Faces[index]);
    }
    ImGui::EndDisabled();

    DrawSmartBrushOptions(context.Brush);
    ImGui::TextDisabled("%zu voxels estimated",
        EstimateSmartBrushGeometry(context.Brush));
    if (context.Pencil.Statistics)
    {
        const SmartBrushStatistics& statistics = *context.Pencil.Statistics;
        DrawSmartBrushStatistics("New", statistics.New, "Existing",
            statistics.Existing, statistics.Total, statistics.Clipped);
    }
}

} // namespace VoxelForge::Editor
