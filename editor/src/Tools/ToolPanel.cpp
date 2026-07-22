#include "ToolPanel.h"

#include "Panels/ErasePanel.h"
#include "Panels/MovePanel.h"
#include "Panels/PaintPanel.h"
#include "Panels/PencilPanel.h"
#include "Panels/RotatePanel.h"
#include "Panels/ScalePanel.h"

#include <imgui.h>

namespace VoxelForge::Editor
{

void ToolPanel::Draw(const ToolManager& manager, ToolContext& context)
{
    const ToolDescriptor& descriptor = manager.ActiveDescriptor();
    ImGui::TextDisabled("Tool");
    ImGui::SameLine();
    ImGui::TextUnformatted(descriptor.Name.data(),
        descriptor.Name.data() + descriptor.Name.size());
    ImGui::Separator();

    ImGui::BeginDisabled(!context.HasDocument);
    switch (descriptor.Panel)
    {
    case ToolPanelKind::Pencil: DrawPencilPanel(context); break;
    case ToolPanelKind::Erase: DrawErasePanel(context); break;
    case ToolPanelKind::Paint: DrawPaintPanel(context); break;
    case ToolPanelKind::Move: DrawMovePanel(context); break;
    case ToolPanelKind::Rotate: DrawRotatePanel(context); break;
    case ToolPanelKind::Scale: DrawScalePanel(context); break;
    case ToolPanelKind::Selection:
        ImGui::TextUnformatted("Selection tool active");
        break;
    case ToolPanelKind::Generic:
    default:
        ImGui::TextDisabled("This tool uses its existing viewport controls.");
        break;
    }
    ImGui::EndDisabled();
    if (!context.HasDocument)
        ImGui::TextDisabled("Open a voxel model to use tool options.");
}

float ToolPanel::PreferredHeight(const ToolPanelKind panel) noexcept
{
    switch (panel)
    {
    case ToolPanelKind::Pencil: return 205.0F;
    case ToolPanelKind::Move:
    case ToolPanelKind::Rotate: return 128.0F;
    case ToolPanelKind::Scale: return 112.0F;
    default: return 82.0F;
    }
}

} // namespace VoxelForge::Editor
