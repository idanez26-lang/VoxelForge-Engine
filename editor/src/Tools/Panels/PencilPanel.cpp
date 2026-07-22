#include "PencilPanel.h"

#include <imgui.h>

#include <algorithm>

namespace VoxelForge::Editor
{

void DrawPencilPanel(ToolContext& context)
{
    ImGui::TextDisabled("Mode");
    int mode = static_cast<int>(context.Pencil.Mode);
    ImGui::RadioButton("Add", &mode, static_cast<int>(PencilMode::Add));
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::RadioButton("Remove", &mode, static_cast<int>(PencilMode::Remove));
    ImGui::SameLine();
    ImGui::RadioButton("Paint", &mode, static_cast<int>(PencilMode::Paint));
    ImGui::EndDisabled();
    context.Pencil.Mode = static_cast<PencilMode>(mode);

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
    int brush = static_cast<int>(context.Pencil.Brush);
    ImGui::RadioButton("Cube", &brush,
        static_cast<int>(VoxelBrushShape::Cube));
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::RadioButton("Sphere", &brush,
        static_cast<int>(VoxelBrushShape::Sphere));
    ImGui::EndDisabled();
    context.Pencil.Brush = static_cast<VoxelBrushShape>(brush);

    ImGui::SetNextItemWidth(90.0F);
    ImGui::BeginDisabled();
    ImGui::InputInt("Size", &context.Pencil.Size);
    ImGui::EndDisabled();
    context.Pencil.Size = std::max(context.Pencil.Size, 1);
}

} // namespace VoxelForge::Editor
