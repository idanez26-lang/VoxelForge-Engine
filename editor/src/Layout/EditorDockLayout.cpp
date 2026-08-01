#include "Layout/EditorDockLayout.h"

#include "Layout/EditorPanelNames.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace VoxelForge::Editor
{

void DrawEditorDockSpace(const ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspaceSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - PanelNames::StatusBarHeight));

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(workspaceSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    ImGui::Begin("##VoxelForgeWorkspaceHost", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGui::DockSpace(
        dockspaceId,
        ImVec2(0.0F, 0.0F),
        ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void BuildDefaultDockLayout(const ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspaceSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - PanelNames::StatusBarHeight));

    ImGui::DockBuilderRemoveNode(dockspaceId);

    const ImGuiDockNodeFlags dockNodeFlags =
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
        ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::DockBuilderAddNode(dockspaceId, dockNodeFlags);
    ImGui::DockBuilderSetNodeSize(dockspaceId, workspaceSize);

    constexpr float PreferredLeftPanelFraction = 0.070F;
    constexpr float PreferredRightPanelFraction = 0.073F;
    constexpr float MinimumSidePanelWidth = 140.0F;
    constexpr float MaximumSidePanelWidth = 220.0F;
    constexpr float MinimumViewportWidthFraction = 0.75F;
    constexpr float PreferredConsoleHeightFraction = 0.10F;
    constexpr float MinimumConsoleHeight = 96.0F;
    constexpr float MaximumConsoleHeight = 140.0F;

    float leftPanelWidth = std::clamp(
        workspaceSize.x * PreferredLeftPanelFraction,
        MinimumSidePanelWidth,
        MaximumSidePanelWidth);
    float rightPanelWidth = std::clamp(
        workspaceSize.x * PreferredRightPanelFraction,
        MinimumSidePanelWidth,
        MaximumSidePanelWidth);
    const float maximumSidePanelTotal = workspaceSize.x *
        (1.0F - MinimumViewportWidthFraction);
    const float requestedSidePanelTotal = leftPanelWidth + rightPanelWidth;
    if (requestedSidePanelTotal > maximumSidePanelTotal &&
        requestedSidePanelTotal > 0.0F)
    {
        const float scale = maximumSidePanelTotal / requestedSidePanelTotal;
        leftPanelWidth *= scale;
        rightPanelWidth *= scale;
    }
    const float consoleHeight = std::clamp(
        workspaceSize.y * PreferredConsoleHeightFraction,
        MinimumConsoleHeight,
        MaximumConsoleHeight);

    ImGuiID topId = dockspaceId;
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Down,
        consoleHeight / workspaceSize.y,
        nullptr,
        &topId);

    ImGuiID rightId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Right,
        rightPanelWidth / workspaceSize.x,
        nullptr,
        &topId);

    const ImGuiID leftId = ImGui::DockBuilderSplitNode(
        topId,
        ImGuiDir_Left,
        leftPanelWidth / (workspaceSize.x - rightPanelWidth),
        nullptr,
        &topId);

    ImGuiID toolsId = leftId;
    const ImGuiID styleId = ImGui::DockBuilderSplitNode(
        toolsId, ImGuiDir_Down, 0.34F, nullptr, &toolsId);
    const ImGuiID toolOptionsId = ImGui::DockBuilderSplitNode(
        toolsId, ImGuiDir_Down, 0.50F, nullptr, &toolsId);

    ImGui::DockBuilderDockWindow(PanelNames::Tools, toolsId);
    ImGui::DockBuilderDockWindow(PanelNames::ToolOptions, toolOptionsId);
    ImGui::DockBuilderDockWindow(PanelNames::Style, styleId);
    ImGui::DockBuilderDockWindow(PanelNames::Viewport, topId);
    ImGui::DockBuilderDockWindow(PanelNames::Assets, rightId);
    ImGui::DockBuilderDockWindow(PanelNames::ForgeLibrary, rightId);
    ImGui::DockBuilderDockWindow(PanelNames::Scene, rightId);
    ImGui::DockBuilderDockWindow(PanelNames::Inspector, rightId);
    ImGui::DockBuilderDockWindow(PanelNames::Transform, rightId);
    ImGui::DockBuilderDockWindow(PanelNames::Console, bottomId);
    if (ImGuiDockNode* const rightNode = ImGui::DockBuilderGetNode(rightId))
        rightNode->SelectedTabId = ImHashStr(PanelNames::Assets);
    ImGui::DockBuilderFinish(dockspaceId);
}

void BuildThumbnailVisualDockLayout(const ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workspaceSize(
        viewport->WorkSize.x,
        std::max(1.0F, viewport->WorkSize.y - PanelNames::StatusBarHeight));
    ImGui::DockBuilderRemoveNode(dockspaceId);
    const ImGuiDockNodeFlags flags =
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
        ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockBuilderAddNode(dockspaceId, flags);
    ImGui::DockBuilderSetNodeSize(dockspaceId, workspaceSize);
    ImGuiID visibleId = dockspaceId;
    static_cast<void>(ImGui::DockBuilderSplitNode(
        visibleId, ImGuiDir_Right, 0.43F, nullptr, &visibleId));
    ImGuiID browserId = visibleId;
    const ImGuiID inspectorId = ImGui::DockBuilderSplitNode(
        browserId, ImGuiDir_Right, 0.36F, nullptr, &browserId);
    ImGui::DockBuilderDockWindow(PanelNames::Assets, browserId);
    ImGui::DockBuilderDockWindow(PanelNames::Inspector, inspectorId);
    ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace VoxelForge::Editor
