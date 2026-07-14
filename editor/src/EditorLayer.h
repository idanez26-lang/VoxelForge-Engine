#pragma once

#include "VoxelForge/Core/Layer/Layer.h"

struct ImGuiViewport;

using ImGuiID = unsigned int;

namespace VoxelForge::Editor
{

class EditorLayer final : public Core::Layer
{
public:
    EditorLayer();

    void OnAttach() override;
    void OnDetach() override;
    void OnImGuiRender() override;

private:
    void BuildDefaultWorkspace(
        ImGuiID dockspaceId,
        const ImGuiViewport& viewport);

    void DrawMainMenuBar();
    void DrawHierarchyPanel();
    void DrawInspectorPanel();
    void DrawViewportPanel();
    void DrawAssetBrowserPanel();
    void DrawConsolePanel();
    void DrawStatusBar();

    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showViewport_ = true;
    bool showAssetBrowser_ = true;
    bool showConsole_ = true;
    bool showAboutPopup_ = false;
    bool resetWorkspaceRequested_ = false;
};

} // namespace VoxelForge::Editor
