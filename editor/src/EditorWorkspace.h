#pragma once

#include "EditorExitRequest.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

using ImGuiID = unsigned int;

namespace VoxelForge::Project
{
class ProjectManager;
}

namespace VoxelForge::Editor
{

class EditorWorkspace final
{
public:
    explicit EditorWorkspace(Project::ProjectManager& projectManager);

    void Draw();
    [[nodiscard]] bool ConsumeExitRequest() noexcept;

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
    void DrawProjectDialogs();
    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();

    void CreateProject();
    void OpenProject(const std::filesystem::path& projectFilePath);
    void SaveProject();
    void CloseProject();

    void AddConsoleMessage(std::string message);
    [[nodiscard]] std::string GetBackendDisplayName() const;

    Project::ProjectManager& projectManager_;
    std::vector<std::string> consoleMessages_;
    EditorExitRequest exitRequest_{};

    std::array<char, 128> newProjectName_{};
    std::array<char, 1024> newProjectParentPath_{};
    std::array<char, 1024> openProjectFilePath_{};
    std::string projectDialogError_;

    bool showExplorer_ = true;
    bool showScene_ = true;
    bool showInspector_ = true;
    bool showAssetBrowser_ = true;
    bool showConsole_ = true;
    bool showProfiler_ = false;
    bool showImGuiDemo_ = false;
    bool showAboutPopup_ = false;
    bool showNewProjectPopup_ = false;
    bool showOpenProjectPopup_ = false;
    bool resetLayoutRequested_ = false;
};

} // namespace VoxelForge::Editor
