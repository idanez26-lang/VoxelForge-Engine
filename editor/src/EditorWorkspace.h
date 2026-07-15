#pragma once

#include "AssetBrowser/AssetBrowser.h"
#include "Commands/CommandHistory.h"
#include "EditorCamera.h"
#include "EditorExitRequest.h"
#include "ViewportRenderer.h"
#include "VoxelViewportState.h"
#include "VoxelSelection/VoxelSelectionState.h"

#include "VoxelForge/Voxel/VoxelGrid.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
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
    using WindowTitleCallback = std::function<bool(std::string)>;

    EditorWorkspace(
        Project::ProjectManager& projectManager,
        WindowTitleCallback windowTitleCallback);
    ~EditorWorkspace();

    void Draw();
    [[nodiscard]] bool ConsumeExitRequest() noexcept;
    [[nodiscard]] bool OpenVoxInViewport(
        const std::filesystem::path& filePath);
    [[nodiscard]] bool HasRenderedVoxelViewport() const noexcept;
    [[nodiscard]] bool HasVoxelViewportRenderError() const noexcept;
    void SetVoxelViewportView(EditorCameraView view) noexcept;
    [[nodiscard]] bool RunVoxelSelectionSmokeStep(std::size_t frame);
    [[nodiscard]] std::size_t VoxelHighlightUploadCount() const noexcept;
    [[nodiscard]] std::size_t VoxelHighlightRenderCount() const noexcept;

private:
    void DrawMainMenuBar();
    void HandleCommandShortcuts();
    void UndoCommand();
    void RedoCommand();
    void DrawDockSpace(ImGuiID dockspaceId);
    void BuildDefaultLayout(ImGuiID dockspaceId);

    void DrawExplorerPanel();
    void DrawScenePanel();
    void DrawWelcomeScreen();
    void DrawInspectorPanel();
    void DrawAssetBrowserPanel();
    void DrawConsolePanel();
    void DrawProfilerPanel();
    void DrawStatusBar();
    void DrawAboutPopup();
    void DrawProjectDialogs();
    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();
    [[nodiscard]] bool DrawPathInput(
        const char* label,
        std::array<char, 1024>& buffer);

    void RequestNewProjectDialog();
    void RequestOpenProjectDialog();

    void CreateProject();
    [[nodiscard]] bool OpenProject(
        const std::filesystem::path& projectFilePath,
        bool recentProject);
    void RemoveRecentProject(
        const std::filesystem::path& projectFilePath);
    void SaveProject();
    void CloseProject();
    void UpdateWindowTitle();
    void ClearVoxelViewport() noexcept;
    void FrameVoxelViewport() noexcept;
    void UpdateVoxelHighlights() noexcept;

    void AddConsoleMessage(std::string message);
    [[nodiscard]] std::string GetBackendDisplayName() const;

    Project::ProjectManager& projectManager_;
    WindowTitleCallback windowTitleCallback_;
    AssetBrowser assetBrowser_;
    EditorCamera viewportCamera_;
    ViewportRenderer viewportRenderer_;
    VoxelViewportState viewportState_;
    std::optional<Voxel::VoxelGrid> viewportGrid_;
    VoxelSelectionState voxelSelection_;
    // Commands are scoped to the current project/model session. Clearing the
    // history before replacement prevents future commands from retaining a
    // handle to an obsolete model.
    CommandHistory commandHistory_;
    Vec3 voxelModelCenter_{};
    std::vector<std::string> consoleMessages_;
    EditorExitRequest exitRequest_{};

    std::array<char, 128> newProjectName_{};
    std::array<char, 1024> newProjectParentPath_{};
    std::array<char, 1024> openProjectFilePath_{};
    std::string projectDialogError_;
    std::string welcomeError_;
    std::optional<std::filesystem::path> failedRecentProjectPath_;

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
    bool voxelViewportRendered_ = false;
    bool voxelViewportRenderFailed_ = false;
    bool voxelSelectionClickCandidate_ = false;
};

} // namespace VoxelForge::Editor
