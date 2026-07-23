#include "PaintPanel.h"
#include "SmartBrushOptions.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

void DrawPaintPanel(ToolContext& context)
{
    ImGui::TextDisabled("Mode");
    ImGui::TextUnformatted("Paint existing voxels");

    DrawSmartBrushOptions(context.Brush);
    ImGui::Spacing();
    ImGui::TextDisabled("%zu voxels estimated",
        EstimateSmartBrushGeometry(context.Brush));
    if (context.Paint.Statistics)
    {
        const VoxelPaintBrushStatistics& statistics =
            *context.Paint.Statistics;
        DrawSmartBrushStatistics("Painted", statistics.Painted, "Ignored",
            statistics.Ignored, statistics.Total, statistics.Clipped);
    }
    ImGui::TextDisabled("Uses the active Palette color.");
}

} // namespace VoxelForge::Editor
