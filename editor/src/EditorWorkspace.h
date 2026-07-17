#pragma once

#include "AssetBrowser/AssetBrowser.h"
#include "AssetInspector/AssetInspectorViewModel.h"
#include "Commands/CommandHistory.h"
#include "Commands/Voxel/AddVoxelCommand.h"
#include "Commands/Voxel/AddVoxelTarget.h"
#include "Commands/Voxel/EraseVoxelCommand.h"
#include "Commands/Voxel/PaintPaletteSelection.h"
#include "Commands/Voxel/PaintVoxelCommand.h"
#include "EditorCamera.h"
#include "EditorExitRequest.h"
#include "DragDropImport/DragDropImportController.h"
#include "Layout/InspectorLayoutModel.h"
#include "ModelImport/ModelImportService.h"
#include "Platform/FileDialogService.h"
#include "Platform/ProjectFolderOpener.h"
#include "Project/ProjectDialogPreferences.h"
#include "Project/QualityOfLifeLogic.h"
#include "ProjectSession/ProjectSessionService.h"
#include "ViewportInput/ViewportCameraInput.h"
#include "ViewportRenderer.h"
#include "VoxelViewportState.h"
#include "VoxelSave/VoxelSaveState.h"
#include "VoxelSave/VoxelDocumentSaveService.h"
#include "VoxelCreation/FirstCreationExperience.h"
#include "VoxelCreation/DirectCreationFlowService.h"
#include "VoxelCreation/VoxelModelCreationService.h"
#include "VoxelCreation/WorkplaneService.h"
#include "VoxelSelection/VoxelSelectionState.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelDocument/VoxelDocumentSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelHistory/VoxelHistoryInput.h"
#include "VoxelTools/VoxelEraserTool.h"
#include "VoxelTools/VoxelPencilInput.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelPencilTool.h"
#include "VoxelTools/VoxelToolState.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"

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
    void BeginFileDrop(float x, float y) noexcept;
    void UpdateFileDropPosition(float x, float y) noexcept;
    void AddDroppedFile(
        const std::filesystem::path& path,
        float x,
        float y);
    void CompleteFileDrop(float x, float y);
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
        const std::filesystem::path& sourcePath,
        bool importVisualSet = false);
    [[nodiscard]] bool ModelImportSmokePassed() const noexcept;
    [[nodiscard]] bool RunDragDropImportSmokeStep(
        std::size_t frame,
        const std::vector<std::filesystem::path>& sourcePaths);
    [[nodiscard]] bool DragDropImportSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelDocumentSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool VoxelDocumentSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelRenderSyncSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool VoxelRenderSyncSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelRayPickingSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool VoxelRayPickingSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelPencilSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool VoxelPencilSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelEraserSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool VoxelEraserSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelUndoRedoSmokeStep(
        std::size_t frame,
        const std::filesystem::path& sourcePath);
    [[nodiscard]] bool VoxelUndoRedoSmokePassed() const noexcept;
    [[nodiscard]] bool RunFirstCreationExperienceSmokeStep(std::size_t frame);
    [[nodiscard]] bool FirstCreationExperienceSmokePassed() const noexcept;
    [[nodiscard]] bool RunLayoutStabilitySmokeStep(std::size_t frame);
    [[nodiscard]] bool LayoutStabilitySmokePassed() const noexcept;
    [[nodiscard]] bool RunDoubleClickCameraSmokeStep(std::size_t frame);
    [[nodiscard]] bool DoubleClickCameraSmokePassed() const noexcept;
    [[nodiscard]] bool RunPersistentWorkplaneSmokeStep(std::size_t frame);
    [[nodiscard]] bool PersistentWorkplaneSmokePassed() const noexcept;
    [[nodiscard]] bool RunProjectSessionRestoreSmokeStep(std::size_t frame);
    [[nodiscard]] bool ProjectSessionRestoreSmokePassed() const noexcept;
    [[nodiscard]] bool RunDirectCreationFlowSmokeStep(std::size_t frame);
    [[nodiscard]] bool DirectCreationFlowSmokePassed() const noexcept;
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
    void BuildThumbnailVisualLayout(ImGuiID dockspaceId);

    void DrawExplorerPanel();
    void DrawScenePanel();
    void DrawWelcomeScreen();
    void DrawInspectorPanel();
    void DrawAssetBrowserPanel();
    void DrawConsolePanel();
    void DrawProfilerPanel();
    void DrawStatusBar();
    void DrawFileDropOverlay(
        const DragDropRect& rect,
        DragDropImportTarget target) const;
    void DrawAboutPopup();
    void DrawProjectDialogs();
    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();
    void DrawModelImportDialogs();
    void DrawVoxelModelCreationDialogs();
    void DrawFirstCreationOverlay();
    void DrawDirtyConfirmationDialog();
    void ConsumeFileDialogResult();
    [[nodiscard]] bool DrawPathInput(
        const char* label,
        std::array<char, 1024>& buffer);

    void RequestNewProjectDialog();
    void RequestOpenProjectDialog();
    void RequestImportModelDialog();
    void RequestNewVoxelModelDialog();
    void RequestCreateVoxelModel(
        VoxelModelCreationRequest request,
        VoxelModelCreationCollisionAction collisionAction);
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
    void CreateVoxelModelNow();
    [[nodiscard]] bool SaveVoxelModel();
    [[nodiscard]] bool HasUnsavedVoxelChanges() const noexcept;
    void CloseProject();
    void SynchronizeProjectAssets();
    [[nodiscard]] bool SaveActiveProjectSession();
    void RestoreActiveProjectSession();
    void BeginModelImport(std::vector<std::filesystem::path> sourcePaths);
    void ContinueModelImport(ModelImportCollisionAction collisionAction);
    void FinishModelImport(bool cancelled = false);
    void LogModelImport(const ModelImportResult& result);
    [[nodiscard]] bool OpenVoxInViewportNow(
        const std::filesystem::path& filePath);
    void OpenProjectFolder();
    void UpdateWindowTitle();
    void ClearVoxelViewport() noexcept;
    void FrameVoxelViewport() noexcept;
    void UpdateVoxelHighlights() noexcept;
    [[nodiscard]] bool SynchronizeVoxelDocumentRendering();
    [[nodiscard]] bool EraseSelectedVoxel();
    [[nodiscard]] bool PaintSelectedVoxel();
    [[nodiscard]] bool AddAdjacentVoxel();
    [[nodiscard]] bool ApplyVoxelPencil();
    [[nodiscard]] bool ApplyVoxelEraser();

    [[nodiscard]] std::uint64_t VoxelModelGeneration() const noexcept override;
    [[nodiscard]] Voxel::VoxelModel* ActiveVoxelModel() noexcept override;
    [[nodiscard]] Asset::Voxel::VoxelDocument*
        ActiveVoxelDocument() noexcept override;
    [[nodiscard]] CommandResult RebuildActiveVoxelMesh() override;
    void CompleteVoxelEdit() noexcept override;
    void UpdateVoxelEditSavedState(bool isAtSavedState) noexcept override;

    void AddConsoleMessage(std::string message);
    [[nodiscard]] std::string GetBackendDisplayName() const;

    Project::ProjectManager& projectManager_;
    WindowTitleCallback windowTitleCallback_;
    AssetBrowser assetBrowser_;
    AssetInspectorViewModel assetInspector_;
    ModelImportService modelImportService_;
    DragDropImportController dragDropImport_;
    EditorCamera viewportCamera_;
    ViewportRenderer viewportRenderer_;
    VoxelViewportState viewportState_;
    VoxelDocumentSession voxelDocumentSession_;
    Mesh::VoxelDocumentMeshCache voxelDocumentMeshCache_;
    std::optional<Voxel::VoxelModel> activeVoxelModel_;
    VoxelSaveState voxelSaveState_;
    VoxelDocumentSaveService voxelDocumentSaveService_;
    VoxelModelCreationService voxelModelCreationService_;
    DirectCreationFlowService directCreationFlowService_;
    FirstCreationExperience firstCreationExperience_;
    WorkplaneService workplaneService_;
    VoxelSelectionState voxelSelection_;
    VoxelToolState voxelToolState_;
    VoxelToolInputController voxelToolInput_;
    VoxelToolInputController voxelToolSmokeInput_;
    VoxelPlacementPreview voxelPlacementPreview_;
    std::optional<WorkplaneHit> workplaneHit_;
    VoxelEditHistory voxelEditHistory_;
    VoxelHistoryInputController voxelHistoryInput_;
    std::optional<VoxelToolResult> lastVoxelToolResult_;
    std::optional<VoxelEraserResult> lastVoxelEraserResult_;
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
    ProjectSessionService projectSessionService_;
    DirtyActionConfirmation dirtyActionConfirmation_;
    std::filesystem::path pendingProjectPath_;
    std::filesystem::path pendingVoxelPath_;
    VoxelModelCreationRequest pendingVoxelModelCreation_{};
    VoxelModelCreationCollisionAction pendingVoxelModelCollisionAction_ =
        VoxelModelCreationCollisionAction::Ask;
    bool pendingRecentProject_ = false;

    std::array<char, 128> newProjectName_{};
    std::array<char, 1024> newProjectParentPath_{};
    std::array<char, 1024> openProjectFilePath_{};
    std::array<char, 128> newVoxelModelName_{};
    std::array<int, 3> newVoxelModelDimensions_{64, 64, 64};
    std::string voxelModelCreationError_;
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
    std::size_t completedImportCount_ = 0U;
    std::size_t skippedImportCount_ = 0U;
    std::size_t failedImportCount_ = 0U;
    DragDropImportTarget pendingDropImportTarget_ =
        DragDropImportTarget::None;
    DragDropRect assetBrowserDropRect_{};
    DragDropRect viewportDropRect_{};
    ViewportRectangle currentViewportRectangle_{};
    bool importStartedFromDrop_ = false;

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
    bool showNewVoxelModelPopup_ = false;
    bool showVoxelModelCollisionPopup_ = false;
    bool showImportConfirmationPopup_ = false;
    bool showImportCollisionPopup_ = false;
    bool showOpenImportedModelPopup_ = false;
    bool showDirtyConfirmationPopup_ = false;
    bool resetLayoutRequested_ = false;
    bool thumbnailVisualLayoutRequested_ = false;
    bool thumbnailVisualMode_ = false;
    bool voxelViewportRendered_ = false;
    bool voxelViewportRenderFailed_ = false;
    bool voxelSelectionClickCandidate_ = false;
    bool voxelEditInProgress_ = false;
    bool viewportFocusRequested_ = false;
    bool viewportFocusApplied_ = false;
    std::filesystem::path firstCreationSmokePath_;
    Asset::Voxel::VoxelPosition firstCreationSmokeTarget_{};
    bool firstCreationSmokeCreated_ = false;
    bool firstCreationSmokePencilled_ = false;
    bool firstCreationSmokeUndone_ = false;
    bool firstCreationSmokeRedone_ = false;
    bool firstCreationSmokeSaved_ = false;
    bool firstCreationSmokeReopened_ = false;
    bool firstCreationSmokeCleaned_ = false;
    std::filesystem::path layoutStabilitySmokePath_;
    Asset::Voxel::VoxelPosition layoutStabilitySmokeTarget_{};
    std::vector<ViewportRectangle> layoutStabilitySmokeRectangles_;
    std::optional<VoxelPickingInteractionState>
        layoutStabilitySmokePickingOverride_;
    std::optional<VoxelRaycastHit> layoutStabilitySmokeHitOverride_;
    std::optional<Asset::Voxel::VoxelPosition>
        layoutStabilitySmokeWorkplaneOverride_;
    bool layoutStabilitySmokeCreated_ = false;
    bool layoutStabilitySmokePencilled_ = false;
    bool layoutStabilitySmokeUndone_ = false;
    bool layoutStabilitySmokeRedone_ = false;
    bool layoutStabilitySmokeRectanglesStable_ = false;
    bool layoutStabilitySmokeCleaned_ = false;
    EditorCameraState doubleClickCameraSmokeReference_{};
    bool doubleClickCameraSmokeGridStable_ = false;
    bool doubleClickCameraSmokeVoxelStable_ = false;
    bool doubleClickCameraSmokeEmptyStable_ = false;
    bool doubleClickCameraSmokeOrbitWorked_ = false;
    bool doubleClickCameraSmokePanWorked_ = false;
    bool doubleClickCameraSmokeZoomWorked_ = false;
    bool doubleClickCameraSmokeShortcutsWorked_ = false;
    bool doubleClickCameraSmokeCleaned_ = false;
    std::filesystem::path persistentWorkplaneSmokePath_;
    Asset::Voxel::VoxelPosition persistentWorkplaneSmokeFirst_{};
    Asset::Voxel::VoxelPosition persistentWorkplaneSmokeSecond_{};
    bool persistentWorkplaneSmokeCreated_ = false;
    bool persistentWorkplaneSmokeFirstAdded_ = false;
    bool persistentWorkplaneSmokeSecondAdded_ = false;
    bool persistentWorkplaneSmokeUndone_ = false;
    bool persistentWorkplaneSmokeRedone_ = false;
    bool persistentWorkplaneSmokeSaved_ = false;
    bool persistentWorkplaneSmokeReopened_ = false;
    bool persistentWorkplaneSmokeCleaned_ = false;
    std::filesystem::path projectSessionSmokeProjectFile_;
    std::filesystem::path projectSessionSmokeModelPath_;
    EditorCameraState projectSessionSmokeCamera_{};
    std::uint64_t projectSessionSmokeRevision_ = 0U;
    std::size_t projectSessionSmokeRefreshCount_ = 0U;
    bool projectSessionSmokeCreated_ = false;
    bool projectSessionSmokeSavedOnClose_ = false;
    bool projectSessionSmokeRestored_ = false;
    bool projectSessionSmokeCleaned_ = false;
    std::filesystem::path directCreationSmokePath_;
    Asset::Voxel::VoxelPosition directCreationSmokeTarget_{};
    bool directCreationSmokeCreated_ = false;
    bool directCreationSmokeFocused_ = false;
    bool directCreationSmokePencilled_ = false;
    bool directCreationSmokeSaved_ = false;
    bool directCreationSmokeCleaned_ = false;
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
    std::uint64_t voxelSaveSmokeRevisionAfterEdit_ = 0U;
    std::uint64_t voxelSaveSmokeFailureBaselineHash_ = 0U;
    bool voxelSaveSmokePaintedAndSaved_ = false;
    bool voxelSaveSmokePaintReloaded_ = false;
    bool voxelSaveSmokeErasedAndSaved_ = false;
    bool voxelSaveSmokeEraseReloaded_ = false;
    bool voxelSaveSmokeRevisionPreserved_ = false;
    bool voxelSaveSmokeMetadataUpdated_ = false;
    bool voxelSaveSmokeThumbnailUpdated_ = false;
    bool voxelSaveSmokeFailureRolledBack_ = false;
    bool voxelSaveSmokeDirtyCloseProtected_ = false;
    bool voxelSaveSmokeCleanupComplete_ = false;
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
    bool modelImportSmokeMetadata_ = false;
    bool modelImportSmokeInspected_ = false;
    bool modelImportSmokeRefreshed_ = false;
    bool modelImportSmokeOpened_ = false;
    bool modelImportSmokeRenamed_ = false;
    bool modelImportSmokeDeleted_ = false;
    bool modelImportSmokeClean_ = false;
    bool modelImportSmokeReanalyzed_ = false;
    bool modelImportSmokeInspectorCleared_ = false;
    bool modelImportSmokeThumbnailGenerated_ = false;
    bool modelImportSmokeThumbnailLoaded_ = false;
    bool modelImportSmokeThumbnailPreserved_ = false;
    bool modelImportSmokeThumbnailRegenerated_ = false;
    bool modelImportSmokeThumbnailRemoved_ = false;
    std::size_t modelImportSmokeRenderBaseline_ = 0U;
    std::filesystem::path modelImportSmokeDestination_;
    std::filesystem::path modelImportSmokeThumbnailPath_;
    std::string modelImportSmokeAssetId_;
    std::vector<std::filesystem::path> dragDropSmokeImportedPaths_;
    bool dragDropSmokeAssetImported_ = false;
    bool dragDropSmokeAssetDidNotOpen_ = false;
    bool dragDropSmokeInspected_ = false;
    bool dragDropSmokeViewportOpened_ = false;
    bool dragDropSmokeCollisionRenamed_ = false;
    bool dragDropSmokeRefreshControlled_ = false;
    bool dragDropSmokeClean_ = false;
    std::uintmax_t voxelDocumentSmokeSourceSize_ = 0U;
    std::uint64_t voxelDocumentSmokeSourceHash_ = 0U;
    std::filesystem::file_time_type voxelDocumentSmokeSourceTime_{};
    bool voxelDocumentSmokeInitialState_ = false;
    bool voxelDocumentSmokeEdited_ = false;
    bool voxelDocumentSmokeSaved_ = false;
    bool voxelDocumentSmokeClosed_ = false;
    bool voxelDocumentSmokeSourcePreserved_ = false;
    std::uintmax_t voxelRenderSyncSmokeSourceSize_ = 0U;
    std::uint64_t voxelRenderSyncSmokeSourceHash_ = 0U;
    std::filesystem::file_time_type voxelRenderSyncSmokeSourceTime_{};
    std::size_t voxelRenderSyncInitialBuildCount_ = 0U;
    std::size_t voxelRenderSyncInitialUploadCount_ = 0U;
    std::size_t voxelRenderSyncStableBuildCount_ = 0U;
    bool voxelRenderSyncInitialBuilt_ = false;
    bool voxelRenderSyncSetRebuilt_ = false;
    bool voxelRenderSyncRemoveRebuilt_ = false;
    bool voxelRenderSyncUnchangedSkipped_ = false;
    bool voxelRenderSyncClosed_ = false;
    bool voxelRenderSyncSourcePreserved_ = false;
    std::uintmax_t voxelRayPickingSmokeSourceSize_ = 0U;
    std::uint64_t voxelRayPickingSmokeSourceHash_ = 0U;
    std::filesystem::file_time_type voxelRayPickingSmokeSourceTime_{};
    std::uint64_t voxelRayPickingInitialRevision_ = 0U;
    std::size_t voxelRayPickingHighlightUploadBaseline_ = 0U;
    bool voxelRayPickingInitialDirty_ = false;
    bool voxelRayPickingRayBuilt_ = false;
    bool voxelRayPickingHitVerified_ = false;
    bool voxelRayPickingHighlightRendered_ = false;
    bool voxelRayPickingMissCleared_ = false;
    bool voxelRayPickingDocumentUnchanged_ = false;
    bool voxelRayPickingClosed_ = false;
    bool voxelRayPickingSourcePreserved_ = false;
    std::uintmax_t voxelPencilSmokeSourceSize_ = 0U;
    std::uint64_t voxelPencilSmokeSourceHash_ = 0U;
    std::filesystem::file_time_type voxelPencilSmokeSourceTime_{};
    std::uint64_t voxelPencilSmokeInitialRevision_ = 0U;
    std::uint64_t voxelPencilSmokeInitialVoxelCount_ = 0U;
    std::size_t voxelPencilSmokeInitialBuildCount_ = 0U;
    std::size_t voxelPencilSmokeInitialUploadCount_ = 0U;
    std::size_t voxelPencilSmokeHighlightUploadBaseline_ = 0U;
    std::size_t voxelPencilSmokeRenderBaseline_ = 0U;
    Asset::Voxel::VoxelPosition voxelPencilSmokeTarget_{};
    bool voxelPencilSmokePreviewValid_ = false;
    bool voxelPencilSmokeApplied_ = false;
    bool voxelPencilSmokeHeldWithoutRepeat_ = false;
    bool voxelPencilSmokeOutOfBoundsRefused_ = false;
    bool voxelPencilSmokeRendered_ = false;
    bool voxelPencilSmokeClosed_ = false;
    bool voxelPencilSmokeSourcePreserved_ = false;
    std::uintmax_t voxelEraserSmokeSourceSize_ = 0U;
    std::uint64_t voxelEraserSmokeSourceHash_ = 0U;
    std::filesystem::file_time_type voxelEraserSmokeSourceTime_{};
    std::uint64_t voxelEraserSmokeInitialRevision_ = 0U;
    std::uint64_t voxelEraserSmokeInitialVoxelCount_ = 0U;
    std::size_t voxelEraserSmokeInitialBuildCount_ = 0U;
    std::size_t voxelEraserSmokeInitialUploadCount_ = 0U;
    std::size_t voxelEraserSmokeHighlightUploadBaseline_ = 0U;
    std::size_t voxelEraserSmokeRenderBaseline_ = 0U;
    Asset::Voxel::VoxelPosition voxelEraserSmokeAddedTarget_{};
    std::uint8_t voxelEraserSmokeRemovedPaletteIndex_ = 0U;
    bool voxelEraserSmokePencilAdded_ = false;
    bool voxelEraserSmokePreviewValid_ = false;
    bool voxelEraserSmokeRemoved_ = false;
    bool voxelEraserSmokeHeldWithoutRepeat_ = false;
    bool voxelEraserSmokeRendered_ = false;
    bool voxelEraserSmokeLastRemoved_ = false;
    bool voxelEraserSmokeClosed_ = false;
    bool voxelEraserSmokeSourcePreserved_ = false;
    std::uintmax_t voxelUndoRedoSmokeSourceSize_ = 0U;
    std::uint64_t voxelUndoRedoSmokeSourceHash_ = 0U;
    std::filesystem::file_time_type voxelUndoRedoSmokeSourceTime_{};
    std::uint64_t voxelUndoRedoSmokeInitialRevision_ = 0U;
    std::uint64_t voxelUndoRedoSmokeInitialVoxelCount_ = 0U;
    std::size_t voxelUndoRedoSmokeInitialBuildCount_ = 0U;
    std::size_t voxelUndoRedoSmokeInitialUploadCount_ = 0U;
    Asset::Voxel::VoxelPosition voxelUndoRedoSmokePencilTarget_{};
    Asset::Voxel::VoxelPosition voxelUndoRedoSmokeBranchTarget_{};
    bool voxelUndoRedoSmokePencilExecuted_ = false;
    bool voxelUndoRedoSmokePencilUndone_ = false;
    bool voxelUndoRedoSmokePencilRedone_ = false;
    bool voxelUndoRedoSmokeEraserCycle_ = false;
    bool voxelUndoRedoSmokeBranchClearedRedo_ = false;
    bool voxelUndoRedoSmokeMultiExecuted_ = false;
    bool voxelUndoRedoSmokeMultiUndone_ = false;
    bool voxelUndoRedoSmokeMultiRedone_ = false;
    bool voxelUndoRedoSmokeClosed_ = false;
    bool voxelUndoRedoSmokeSourcePreserved_ = false;
    std::optional<std::uint64_t> uploadedDocumentIdentity_;
    std::optional<std::uint64_t> uploadedDocumentRevision_;
    std::optional<std::uint64_t> failedDocumentIdentity_;
    std::optional<std::uint64_t> failedDocumentRevision_;
};

} // namespace VoxelForge::Editor
