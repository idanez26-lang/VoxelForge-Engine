#include "PencilPanel.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace VoxelForge::Editor
{

void DrawPencilPanel(ToolContext& context)
{
    ImGui::TextDisabled("Mode");
    int mode = static_cast<int>(context.Pencil.State.Mode);
    ImGui::RadioButton("Add", &mode, static_cast<int>(SmartBrushMode::Add));
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::RadioButton("Remove", &mode, static_cast<int>(SmartBrushMode::Erase));
    ImGui::SameLine();
    ImGui::RadioButton("Paint", &mode, static_cast<int>(SmartBrushMode::Paint));
    ImGui::EndDisabled();
    context.Pencil.State.Mode = static_cast<SmartBrushMode>(mode);

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

    ImGui::TextDisabled("Brush");
    int brush = static_cast<int>(context.Pencil.State.Shape);
    ImGui::RadioButton("Cube", &brush,
        static_cast<int>(SmartBrushShape::Cube));
    ImGui::SameLine();
    ImGui::RadioButton("Sphere", &brush,
        static_cast<int>(SmartBrushShape::Sphere));
    context.Pencil.State.Shape = static_cast<SmartBrushShape>(brush);

    ImGui::SetNextItemWidth(90.0F);
    ImGui::InputInt("Size", &context.Pencil.State.Size);
    context.Pencil.State.Size = std::clamp(
        context.Pencil.State.Size, 1, SmartBrushEngine::MaximumSize());
    ImGui::TextDisabled("%zu voxels estimated",
        SmartBrushEngine::EstimateTotal(context.Pencil.State));
}

} // namespace VoxelForge::Editor
