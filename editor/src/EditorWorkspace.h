#pragma once

#include "AssetBrowser/AssetBrowser.h"
#include "Commands/CommandHistory.h"
#include "Commands/Voxel/AddVoxelCommand.h"
#include "Commands/Voxel/AddVoxelTarget.h"
#include "Commands/Voxel/EraseVoxelCommand.h"
#include "Commands/Voxel/PaintPaletteSelection.h"
#include "Commands/Voxel/PaintVoxelCommand.h"
#include "EditorCamera.h"
#include "EditorExitRequest.h"
#include "ModelImport/ModelImportService.h"
#include "Platform/FileDialogService.h"
#include "Platform/ProjectFolderOpener.h"
#include "Project/ProjectDialogPreferences.h"
#include "Project/QualityOfLifeLogic.h"
#include "ViewportRenderer.h"
#include "VoxelViewportState.h"
#include "VoxelSave/VoxelSaveState.h"
#include "VoxelSelection/VoxelSelectionState.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <vector>

using ImGuiID = unsigned int;

namespace VoxelForge::Project
{
class ProjectManager;
}

namespace VoxelForge::Editor
{

class EditorWorkspace final : private VoxelEditSession
{
public:
    using WindowTitleCallback = std::function<bool(std::string)>;

    EditorWorkspace(
        Project::ProjectManager& projectManager,
        WindowTitleCallback windowTitleCallback,
        std::filesystem::path preferencesFilePath =
            ProjectDialogPreferences::DefaultStorageFilePath(),
        bool simulatedFileDialogs = false);
    ~EditorWorkspace();

    void Draw();
    [[nodiscard]] bool ConsumeExitRequest() noexcept;
    [[nodiscard]] bool RequestApplicationExit();
    [[nodiscard]] bool OpenVoxInViewport(
        const std::filesystem::path& filePath);
    [[nodiscard]] bool HasRenderedVoxelViewport() const noexcept;
    [[nodiscard]] bool HasVoxelViewportRenderError() const noexcept;
    void SetVoxelViewportView(EditorCameraView view) noexcept;
    [[nodiscard]] bool RunVoxelSelectionSmokeStep(std::size_t frame);
    [[nodiscard]] bool RunEraseVoxelSmokeStep(std::size_t frame);
    [[nodiscard]] bool EraseVoxelSmokePassed() const noexcept;
    [[nodiscard]] bool RunPaintVoxelSmokeStep(std::size_t frame);
    [[nodiscard]] bool PaintVoxelSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelSaveSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelSaveSmokePassed() const noexcept;
    [[nodiscard]] bool RunAddVoxelSmokeStep(std::size_t frame);
    [[nodiscard]] bool AddVoxelSmokePassed() const noexcept;
    [[nodiscard]] bool RunModelImportSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool ModelImportSmokePassed() const noexcept;
    [[nodiscard]] bool RunQualityOfLifeSmokeStep(
        std::size_t frame,
        const std::filesystem::path& parentDirectory);
    [[nodiscard]] bool QualityOfLifeSmokePassed() const noexcept;
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
    void DrawModelImportDialogs();
    void DrawDirtyConfirmationDialog();
    void ConsumeFileDialogResult();
    [[nodiscard]] bool DrawPathInput(
        const char* label,
        std::array<char, 1024>& buffer);

    void RequestNewProjectDialog();
    void RequestOpenProjectDialog();
    void RequestImportModelDialog();
    void RequestExit();
    void RequestCloseProject();
    void RequestOpenProject(
        std::filesystem::path projectFilePath,
        bool recentProject);
    void RequestReplaceVoxelModel(std::filesystem::path filePath);
    void ExecutePendingDirtyAction(DestructiveAction action);

    void CreateProject();
    void CreateProjectNow();
    [[nodiscard]] bool OpenProject(
        const std::filesystem::path& projectFilePath,
        bool recentProject);
    void RemoveRecentProject(
        const std::filesystem::path& projectFilePath);
    void SaveProject();
    [[nodiscard]] bool SaveVoxelModel();
    void CloseProject();
    void SynchronizeProjectAssets();
    void BeginModelImport(std::vector<std::filesystem::path> sourcePaths);
    void ContinueModelImport(ModelImportCollisionAction collisionAction);
    void FinishModelImport();
    void LogModelImport(const ModelImportResult& result);
    [[nodiscard]] bool OpenVoxInViewportNow(
        const std::filesystem::path& filePath);
    void OpenProjectFolder();
    void UpdateWindowTitle();
    void ClearVoxelViewport() noexcept;
    void FrameVoxelViewport() noexcept;
    void UpdateVoxelHighlights() noexcept;
    [[nodiscard]] bool EraseSelectedVoxel();
    [[nodiscard]] bool PaintSelectedVoxel();
    [[nodiscard]] bool AddAdjacentVoxel();

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override;
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override;
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override;
    void CompleteVoxelEdit() noexcept override;

    void AddConsoleMessage(std::string message);
    [[nodiscard]] std::string GetBackendDisplayName() const;

    Project::ProjectManager& projectManager_;
    WindowTitleCallback windowTitleCallback_;
    AssetBrowser assetBrowser_;
    ModelImportService modelImportService_;
    EditorCamera viewportCamera_;
    ViewportRenderer viewportRenderer_;
    VoxelViewportState viewportState_;
    std::optional<Voxel::VoxelModel> activeVoxelModel_;
    VoxelSaveState voxelSaveState_;
    VoxelSelectionState voxelSelection_;
    PaintPaletteSelection paintPaletteSelection_;
    // Commands are scoped to the current project/model session. Clearing the
    // history before replacement prevents future commands from retaining a
    // handle to an obsolete model.
    CommandHistory commandHistory_;
    std::uint64_t voxelModelGeneration_ = 0U;
    Vec3 voxelModelCenter_{};
    std::vector<std::string> consoleMessages_;
    EditorExitRequest exitRequest_{};
    std::unique_ptr<FileDialogService> fileDialogService_;
    std::unique_ptr<ProjectFolderOpener> projectFolderOpener_;
    ProjectDialogPreferences projectDialogPreferences_;
    DirtyActionConfirmation dirtyActionConfirmation_;
    std::filesystem::path pendingProjectPath_;
    std::filesystem::path pendingVoxelPath_;
    bool pendingRecentProject_ = false;

    std::array<char, 128> newProjectName_{};
    std::array<char, 1024> newProjectParentPath_{};
    std::array<char, 1024> openProjectFilePath_{};
    std::string projectDialogError_;
    std::string welcomeError_;
    std::optional<std::filesystem::path> failedRecentProjectPath_;
    std::vector<std::filesystem::path> selectedImportPaths_;
    std::vector<std::filesystem::path> pendingImportPaths_;
    std::vector<std::filesystem::path> successfulImportPaths_;
    std::optional<ModelImportResult> pendingImportCollision_;
    std::optional<std::filesystem::path> importedModelToOpen_;
    std::size_t pendingImportIndex_ = 0U;
    std::size_t requestedImportCount_ = 0U;

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
    bool showImportConfirmationPopup_ = false;
    bool showImportCollisionPopup_ = false;
    bool showOpenImportedModelPopup_ = false;
    bool showDirtyConfirmationPopup_ = false;
    bool resetLayoutRequested_ = false;
    bool voxelViewportRendered_ = false;
    bool voxelViewportRenderFailed_ = false;
    bool voxelSelectionClickCandidate_ = false;
    bool eraseSmokeSelected_ = false;
    bool eraseSmokeExecuted_ = false;
    bool eraseSmokeUndone_ = false;
    bool eraseSmokeRedone_ = false;
    bool paintSmokeSelected_ = false;
    bool paintSmokeExecuted_ = false;
    bool paintSmokeUndone_ = false;
    bool paintSmokeRedone_ = false;
    bool qualityOfLifeSmokePassed_ = false;
    std::size_t eraseSmokeInitialVoxelCount_ = 0U;
    std::size_t eraseSmokeEraseRenderBaseline_ = 0U;
    std::size_t eraseSmokeUndoRenderBaseline_ = 0U;
    std::size_t eraseSmokeRedoRenderBaseline_ = 0U;
    std::size_t paintSmokeInitialVoxelCount_ = 0U;
    std::size_t paintSmokeInitialTriangleCount_ = 0U;
    std::size_t paintSmokeExecuteRenderBaseline_ = 0U;
    std::size_t paintSmokeUndoRenderBaseline_ = 0U;
    std::size_t paintSmokeRedoRenderBaseline_ = 0U;
    std::uint32_t paintSmokeX_ = 0U;
    std::uint32_t paintSmokeY_ = 0U;
    std::uint32_t paintSmokeZ_ = 0U;
    std::uint8_t paintSmokeInitialColor_ = 0U;
    std::uint8_t paintSmokeNewColor_ = 0U;
    std::filesystem::path voxelSaveSmokePath_;
    std::size_t voxelSaveSmokeInitialVoxelCount_ = 0U;
    std::size_t voxelSaveSmokeRenderBaseline_ = 0U;
    std::uint32_t voxelSaveSmokeX_ = 0U;
    std::uint32_t voxelSaveSmokeY_ = 0U;
    std::uint32_t voxelSaveSmokeZ_ = 0U;
    std::uint8_t voxelSaveSmokeColor_ = 0U;
    bool voxelSaveSmokePaintedAndSaved_ = false;
    bool voxelSaveSmokePaintReloaded_ = false;
    bool voxelSaveSmokeErasedAndSaved_ = false;
    bool voxelSaveSmokeEraseReloaded_ = false;
    std::filesystem::path addVoxelSmokeSavePath_;
    VoxelCoordinates addVoxelSmokeTarget_{};
    std::size_t addVoxelSmokePreviewUploadBaseline_ = 0U;
    std::size_t addVoxelSmokeExecuteRenderBaseline_ = 0U;
    std::size_t addVoxelSmokeUndoRenderBaseline_ = 0U;
    std::size_t addVoxelSmokeReloadRenderBaseline_ = 0U;
    bool addVoxelSmokeSelected_ = false;
    bool addVoxelSmokeExecuted_ = false;
    bool addVoxelSmokeUndone_ = false;
    bool addVoxelSmokeRedoneAndSaved_ = false;
    bool addVoxelSmokeReloaded_ = false;
    bool modelImportSmokeImported_ = false;
    bool modelImportSmokeRefreshed_ = false;
    bool modelImportSmokeOpened_ = false;
    std::size_t modelImportSmokeRenderBaseline_ = 0U;
    std::filesystem::path modelImportSmokeDestination_;
};

} // namespace VoxelForge::Editor
