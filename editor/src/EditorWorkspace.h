#pragma once

#include <string>
#include <vector>

using ImGuiID = unsigned int;

namespace VoxelForge::Editor
{

class EditorWorkspace final
{
public:
    EditorWorkspace();

    void Draw();

private:
    void DrawMainMenuBar();
    void DrawDockSpace(ImGuiID dockspaceId);
    void BuildDefaultLayout(ImGuiID dockspaceId);

    void DrawExplorerPanel();
    void DrawScenePanel();
    void DrawInspectorPanel();
    void DrawAssetBrowserPanel();
    void DrawConsolePanel();
    void DrawProfilerPanel();
    void DrawStatusBar();
    void DrawAboutPopup();

    void AddConsoleMessage(std::string message);
    [[nodiscard]] std::string GetBackendDisplayName() const;

    std::vector<std::string> consoleMessages_;

    bool showExplorer_ = true;
    bool showScene_ = true;
    bool showInspector_ = true;
    bool showAssetBrowser_ = true;
    bool showConsole_ = true;
    bool showProfiler_ = false;
    bool showImGuiDemo_ = false;
    bool showAboutPopup_ = false;
    bool resetLayoutRequested_ = false;
};

} // namespace VoxelForge::Editor
