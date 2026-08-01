#pragma once

#include "AssetBrowser/AssetBrowser.h"
#include "AssetInspector/AssetInspectorViewModel.h"
#include "Commands/CommandHistory.h"
#include "Console/EditorConsoleService.h"
#include "Commands/Voxel/AddVoxelCommand.h"
#include "Commands/Voxel/AddVoxelTarget.h"
#include "Commands/Voxel/EraseVoxelCommand.h"
#include "Commands/Voxel/PaintPaletteSelection.h"
#include "Commands/Voxel/PaintVoxelCommand.h"
#include "Constraints/ConstraintEngine.h"
#include "EditorCamera.h"
#include "EditorCloseRequest.h"
#include "EditorExitRequest.h"
#include "Input/EditorInputService.h"
#include "DragDropImport/DragDropImportController.h"
#include "Layout/InspectorLayoutModel.h"
#include "ModelImport/ModelImportService.h"
#include "Palette/PaletteService.h"
#include "Platform/FileDialogService.h"
#include "Platform/ProjectFolderOpener.h"
#include "Project/ProjectDialogPreferences.h"
#include "Project/QualityOfLifeLogic.h"
#include "ProjectSession/ProjectSessionService.h"
#include "Preview/UniversalCursor2D.h"
#include "Preview/VoxelPreview.h"
#include "Selection/SelectionService.h"
#include "Selection/SelectionInteraction.h"
#include "Selection/SelectionHandleModel.h"
#include "Selection/SelectionHighlightPolicy.h"
#include "Selection/SelectionVolumeCache.h"
#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolFaceDepthDrag.h"
#include "SmartTools/SmartToolLineConstraintResolver.h"
#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "SmartTools/SmartToolRequest.h"
#include "SmartTools/SmartToolStroke.h"
#include "SmartTools/SmartPreviewEngine.h"
#include "SmartTools/BrushProfileService.h"
#include "Transform/TransformPreviewModel.h"
#include "Transform/TransformPivotManager.h"
#include "TransformGizmo/TransformGizmoInteraction.h"
#include "TransformGizmo/TransformGizmoManager.h"
#include "TransformGizmo/TransformGizmoModel.h"
#include "Transform/AlignVoxelSelectionOperation.h"
#include "Transform/MoveVoxelSelectionOperation.h"
#include "Transform/DuplicateVoxelSelectionOperation.h"
#include "Transform/MirrorVoxelSelectionOperation.h"
#include "Transform/RotateVoxelSelectionOperation.h"
#include "Transform/ScaleVoxelSelectionOperation.h"
#include "TransformPanel/TransformPanelViewModel.h"
#include "Tools/ToolContext.h"
#include "Tools/ToolManager.h"
#include "ViewportInput/ViewportCameraInput.h"
#include "ViewportNavigationController.h"
#include "ViewportRenderer.h"
#include "ViewportInteractionV2/PencilCompactChangeResolver.h"
#include "ViewportInteractionV2/PencilViewportInteractionController.h"
#include "ViewportInteractionV2/ViewportInteractionController.h"
#include "VoxelViewportState.h"
#include "VoxelSave/VoxelSaveState.h"
#include "VoxelSave/VoxelDocumentSaveService.h"
#include "VoxelCreation/FirstCreationExperience.h"
#include "VoxelCreation/DirectCreationFlowService.h"
#include "VoxelCreation/NewVoxelModelWorkflow.h"
#include "VoxelCreation/VoxelModelCreationService.h"
#include "VoxelCreation/WorkplaneService.h"
#include "VoxelSelection/VoxelSelectionState.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelStamps/Library/StampCatalogService.h"
#include "VoxelStamps/Library/ForgeLibraryPanel.h"
#include "VoxelStamps/Library/ForgeLibraryViewModel.h"
#include "VoxelStamps/Library/StampJsonCatalogStore.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Workflow/SaveSelectionAsStampWorkflow.h"
#include "VoxelStamps/Workflow/StampPreviewController.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelDocument/VoxelDocumentSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelTools/VoxelEraserTool.h"
#include "VoxelTools/VoxelBoxService.h"
#include "VoxelTools/VoxelFillService.h"
#include "VoxelTools/VoxelLineService.h"
#include "VoxelTools/VoxelPaintBrushTool.h"
#include "VoxelTools/VoxelSphereService.h"
#include "VoxelTools/VoxelPencilInput.h"
#include "VoxelTools/VoxelPencilPreview.h"
#include "VoxelTools/VoxelPencilTool.h"
#include "VoxelTools/VoxelToolState.h"
#include "Welcome/ProjectDeletionService.h"
#include "Welcome/WelcomeScreenModel.h"

#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"

#include <array>
#include <chrono>
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
    [[nodiscard]] bool SelectionSystemSmokePassed() const noexcept;
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
    [[nodiscard]] bool RunCreateWorkspaceSmokeStep(std::size_t frame);
    [[nodiscard]] bool CreateWorkspaceSmokePassed() const noexcept;
    [[nodiscard]] bool RunDoubleClickCameraSmokeStep(std::size_t frame);
    [[nodiscard]] bool DoubleClickCameraSmokePassed() const noexcept;
    [[nodiscard]] bool RunPersistentWorkplaneSmokeStep(std::size_t frame);
    [[nodiscard]] bool PersistentWorkplaneSmokePassed() const noexcept;
    [[nodiscard]] bool RunProjectSessionRestoreSmokeStep(std::size_t frame);
    [[nodiscard]] bool ProjectSessionRestoreSmokePassed() const noexcept;
    [[nodiscard]] bool RunDirectCreationFlowSmokeStep(std::size_t frame);
    [[nodiscard]] bool DirectCreationFlowSmokePassed() const noexcept;
    [[nodiscard]] bool RunPaletteUiSmokeStep(std::size_t frame);
    [[nodiscard]] bool PaletteUiSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelFillSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelFillSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelBoxSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelBoxSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelLineSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelLineSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelSphereSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelSphereSmokePassed() const noexcept;
    [[nodiscard]] bool RunModernToolbarSmokeStep(std::size_t frame);
    [[nodiscard]] bool ModernToolbarSmokePassed() const noexcept;
    [[nodiscard]] bool RunKeyboardShortcutsSmokeStep(std::size_t frame);
    [[nodiscard]] bool KeyboardShortcutsSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelMoveSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelMoveSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelDuplicateSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelDuplicateSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelRotateSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelRotateSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelMirrorSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelMirrorSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelScaleSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelScaleSmokePassed() const noexcept;
    [[nodiscard]] bool RunVoxelAlignSmokeStep(std::size_t frame);
    [[nodiscard]] bool VoxelAlignSmokePassed() const noexcept;
    [[nodiscard]] bool RunTransformGizmoFoundationSmokeStep(
        std::size_t frame);
    [[nodiscard]] bool TransformGizmoFoundationSmokePassed() const noexcept;
    [[nodiscard]] bool RunMoveGizmoSmokeStep(std::size_t frame);
    [[nodiscard]] bool MoveGizmoSmokePassed() const noexcept;
    [[nodiscard]] bool RunRotateGizmoSmokeStep(std::size_t frame);
    [[nodiscard]] bool RotateGizmoSmokePassed() const noexcept;
    [[nodiscard]] bool RunScaleGizmoSmokeStep(std::size_t frame);
    [[nodiscard]] bool ScaleGizmoSmokePassed() const noexcept;
    [[nodiscard]] bool RunTransformGizmoManagerSmokeStep(std::size_t frame);
    [[nodiscard]] bool TransformGizmoManagerSmokePassed() const noexcept;
    [[nodiscard]] bool RunTransformPanelSmokeStep(std::size_t frame);
    [[nodiscard]] bool TransformPanelSmokePassed() const noexcept;
    [[nodiscard]] bool RunSaveOnExitSmokeStep(std::size_t frame);
    [[nodiscard]] bool SaveOnExitSmokePassed() const noexcept;
    [[nodiscard]] bool RunQualityOfLifeSmokeStep(
        std::size_t frame,
        const std::filesystem::path& parentDirectory);
    [[nodiscard]] bool QualityOfLifeSmokePassed() const noexcept;
    [[nodiscard]] bool RunStampLivePreviewVisualStep(std::size_t frame);
    [[nodiscard]] bool RunStampPlacementVisualStep(std::size_t frame);
    [[nodiscard]] bool RunForgeLibraryVisualStep(std::size_t frame);
    [[nodiscard]] std::size_t VoxelHighlightUploadCount() const noexcept;
    [[nodiscard]] std::size_t VoxelHighlightRenderCount() const noexcept;

private:
    void DrawMainMenuBar();
    void HandleCommandShortcuts();
    void ExecuteInputCommand(EditorInputCommand command);
    void SelectVoxelTool(ActiveVoxelTool tool);
    void CancelActiveInteraction();
    [[nodiscard]] EditorCommandAvailability CurrentCommandAvailability() const;
    [[nodiscard]] bool CanMoveSelection() const noexcept;
    [[nodiscard]] bool CanDuplicateSelection() const noexcept;
    [[nodiscard]] bool CanRotateSelection() const noexcept;
    [[nodiscard]] bool CanMirrorSelection() const noexcept;
    [[nodiscard]] bool CanScaleSelection() const noexcept;
    [[nodiscard]] bool CanAlignSelection() const noexcept;
    void ApplyVoxelHistorySelection(const VoxelEditHistoryResult& result);
    void UndoCommand();
    void RedoCommand();
    void ApplyDefaultLayoutPanelVisibility();
    void ApplyThumbnailVisualLayoutPanelVisibility();

    void DrawExplorerPanel();
    void DrawToolsPanel();
    void DrawToolOptionsPanel();
    void DrawScenePanel();
    void DrawWelcomeScreen();
    void DrawInspectorPanel();
    void DrawTransformPanel();
    void DrawPalettePanel();
    void DrawAssetBrowserPanel();
    void DrawForgeLibraryPanel();
    void DrawConsolePanel();
    void DrawProfilerPanel();
    void DrawStatusBar();
    void DrawFileDropOverlay(
        const DragDropRect& rect,
        DragDropImportTarget target) const;
    void DrawAboutPopup();
    void DrawProjectDialogs();
    void DrawProjectDeletionDialog();
    void DrawNewProjectDialog();
    void DrawOpenProjectDialog();
    void DrawModelImportDialogs();
    void DrawVoxelModelCreationDialogs();
    void DrawFirstCreationOverlay();
    void DrawDirtyConfirmationDialog();
    void DrawSaveSelectionAsStampDialog();
    void BeginSaveSelectionAsStamp();
    void MoveLatestStampPreview(std::int32_t x, std::int32_t y, std::int32_t z);
    void RotateLatestStampPreview(bool clockwise);
    void MirrorLatestStampPreview(Stamps::StampPlacementMirrorMode mirror);
    void PlaceLatestStampPreview();
    [[nodiscard]] bool RefreshLatestStampPreview();
    void ClearLatestStampPreview() noexcept;
    void ConsumeFileDialogResult();
    [[nodiscard]] bool DrawPathInput(
        const char* label,
        std::array<char, 1024>& buffer);

    void RequestNewProjectDialog();
    void RequestOpenProjectDialog();
    void RequestImportModelDialog();
    void RequestNewVoxelModelDialog();
    void RequestInstantNewVoxelModel();
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
    void ProcessDeferredDirtyActionAtFrameStart();
    void CompleteDeferredCloseAfterFrame();
    void PrepareForApplicationClose();

    void CreateProject();
    void CreateProjectNow();
    [[nodiscard]] bool OpenProject(
        const std::filesystem::path& projectFilePath,
        bool recentProject);
    void RemoveRecentProject(
        const std::filesystem::path& projectFilePath);
    void RequestDeleteProject(
        const std::filesystem::path& projectFilePath);
    void DeletePendingProject();
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
    void FocusSelectionOrFrameAll() noexcept;
    [[nodiscard]] ViewportNavigationBounds
        SelectionNavigationBounds() const noexcept;
    [[nodiscard]] ViewportNavigationBounds SceneNavigationBounds() const noexcept;
    void UpdateVoxelHighlights() noexcept;
    void UpdateTransformGizmo(float viewportHeightPixels) noexcept;
    void DrawTransformGizmoVisibilityAnchor() const noexcept;
    void DrawUniversalPreviewCursor2D() const noexcept;
    void DrawViewportInteractionV2Overlay() const noexcept;
    void CommitViewportInteractionV2Move();
    void CommitPencilViewportInteractionV2();
    [[nodiscard]] bool SynchronizeVoxelDocumentRendering();
    [[nodiscard]] bool EraseSelectedVoxel();
    [[nodiscard]] bool PaintSelectedVoxel();
    [[nodiscard]] bool AddAdjacentVoxel();
    // Re-picks exclusively against committed source geometry. Pending Smart
    // Tool cells remain planner input through SmartToolStroke::ReadVoxel, but
    // never become picking faces during the same gesture.
    [[nodiscard]] bool RefreshSmartToolHover() noexcept;
    [[nodiscard]] std::optional<SmartToolRequest> BuildSmartPencilRequest(
        const SmartToolStroke* stroke = nullptr);
    [[nodiscard]] std::optional<PencilCompactRequest>
        BuildPencilCompactRequest();
    [[nodiscard]] bool BeginSmartToolStroke();
    [[nodiscard]] bool ContinueSmartToolStroke();
    [[nodiscard]] bool CommitSmartToolStroke();
    void CancelSmartToolStroke() noexcept;
    [[nodiscard]] bool ApplySmartFill();
    [[nodiscard]] bool ApplyVoxelPencil();
    [[nodiscard]] bool ApplyVoxelEraser();
    [[nodiscard]] bool ApplyVoxelPaintBrush();
    [[nodiscard]] bool ApplyVoxelFill();
    [[nodiscard]] bool ApplyVoxelBox();
    [[nodiscard]] bool ApplyVoxelLine();
    [[nodiscard]] bool ApplyVoxelSphere();
    [[nodiscard]] Asset::Voxel::VoxelPosition ConstrainMoveDelta(
        Asset::Voxel::VoxelPosition delta) const noexcept;
    [[nodiscard]] std::int32_t ConstrainRotationQuarterTurns(
        VoxelRotationAxis axis, std::int32_t quarterTurns) const noexcept;
    [[nodiscard]] bool ApplyVoxelMove();
    [[nodiscard]] bool ApplyVoxelDuplicate();
    [[nodiscard]] bool BeginVoxelRotatePreview(
        VoxelRotationDirection direction);
    [[nodiscard]] bool BeginVoxelRotatePreview(
        VoxelRotationAxis axis, std::int32_t quarterTurns);
    [[nodiscard]] bool ApplyVoxelRotate();
    void CancelVoxelRotate() noexcept;
    [[nodiscard]] bool BeginVoxelMirrorPreview(VoxelMirrorAxis axis);
    [[nodiscard]] bool ApplyVoxelMirror();
    void CancelVoxelMirror() noexcept;
    [[nodiscard]] bool BeginVoxelScalePreview(VoxelScaleMode mode);
    [[nodiscard]] bool UpdateVoxelScalePreview(
        VoxelScaleMode mode,
        Asset::Voxel::VoxelDimensions targetDimensions);
    [[nodiscard]] bool ApplyVoxelScale();
    void CancelVoxelScale() noexcept;
    [[nodiscard]] bool BeginVoxelAlignPreview(VoxelAlignDirection direction);
    [[nodiscard]] bool ApplyVoxelAlign();
    void CancelVoxelAlign() noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition>
        CurrentTwoPointToolTarget() const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::VoxelPosition>
        CurrentSelectionTarget() const noexcept;
    [[nodiscard]] bool ApplySelectionBounds(
        SelectionBounds bounds, SelectionMode mode);
    void CancelSelectionInteraction();
    void CancelTransformGizmoInteraction() noexcept;
    [[nodiscard]] TransformPanelSource CurrentTransformPanelSource() noexcept;
    [[nodiscard]] bool ApplyTransformPanelPosition(Vec3 position);
    [[nodiscard]] bool ApplyTransformPanelRotation(Vec3 degrees);
    [[nodiscard]] bool ApplyTransformPanelScale(Vec3 scale);
    void CancelVoxelBox() noexcept;
    void CancelVoxelLine() noexcept;
    void CancelVoxelSphere() noexcept;

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
    PaletteService paletteService_;
    ModelImportService modelImportService_;
    DragDropImportController dragDropImport_;
    EditorCamera viewportCamera_;
    ViewportNavigationController viewportNavigation_{viewportCamera_};
    ViewportRenderer viewportRenderer_;
    InteractionV2::ViewportInteractionController viewportInteractionV2_;
    InteractionV2::PencilViewportInteractionController pencilViewportInteractionV2_;
    VoxelViewportState viewportState_;
    VoxelDocumentSession voxelDocumentSession_;
    Mesh::VoxelDocumentMeshCache voxelDocumentMeshCache_;
    std::optional<Voxel::VoxelModel> activeVoxelModel_;
    VoxelSaveState voxelSaveState_;
    VoxelDocumentSaveService voxelDocumentSaveService_;
    VoxelModelCreationService voxelModelCreationService_;
    DirectCreationFlowService directCreationFlowService_;
    NewVoxelModelWorkflow newVoxelModelWorkflow_;
    FirstCreationExperience firstCreationExperience_;
    WorkplaneService workplaneService_;
    SelectionService selectionService_;
    Stamps::StampProjectLibraryRepository stampProjectLibraryRepository_;
    Stamps::StampJsonCatalogStore stampJsonCatalogStore_;
    Stamps::StampCatalogService stampCatalogService_{
        stampProjectLibraryRepository_, stampJsonCatalogStore_};
    Stamps::SaveSelectionAsStampWorkflow saveSelectionAsStampWorkflow_{
        stampProjectLibraryRepository_, stampJsonCatalogStore_};
    Stamps::StampPlacementSession stampPlacementSession_;
    Stamps::ForgeLibraryViewModel forgeLibraryViewModel_{
        stampCatalogService_, stampProjectLibraryRepository_,
        stampPlacementSession_};
    Stamps::ForgeLibraryPanel forgeLibraryPanel_{forgeLibraryViewModel_};
    std::uint64_t stampLivePreviewVisualDocumentRevision_ = 0U;
    std::optional<std::chrono::steady_clock::time_point>
        stampLivePreviewVisualStartedAt_;
    bool stampLivePreviewVisualValid_ = false;
    bool stampLivePreviewVisualOverlap_ = false;
    bool stampLivePreviewVisualClear_ = false;
    std::uint64_t stampPlacementVisualDocumentRevision_ = 0U;
    std::optional<std::chrono::steady_clock::time_point>
        stampPlacementVisualStartedAt_;
    bool stampPlacementVisualPreviewed_ = false;
    bool stampPlacementVisualMirroredX_ = false;
    bool stampPlacementVisualMirroredZ_ = false;
    bool stampPlacementVisualMirroredXZ_ = false;
    bool stampPlacementVisualMirrorRotated_ = false;
    bool stampPlacementVisualFirstPlaced_ = false;
    bool stampPlacementVisualMoved_ = false;
    bool stampPlacementVisualSecondPlaced_ = false;
    bool stampPlacementVisualUndone_ = false;
    bool stampPlacementVisualRedone_ = false;
    bool stampPlacementVisualCleared_ = false;
    bool stampPlacementVisualFinished_ = false;
    bool stampPlacementVisualSucceeded_ = false;
    std::array<char, 256> saveSelectionAsStampName_{};
    std::string saveSelectionAsStampMessage_;
    bool showSaveSelectionAsStampPopup_ = false;
    SelectionVolumeCache selectionVolumeCache_;
    SelectionInteraction selectionInteraction_;
    ConstraintSettings constraintSettings_{
        true, GridConstraintStep::One,
        true, RotationConstraintStep::Degrees90};
    TransformPreviewModel transformPreviewModel_;
    TransformGizmoInteraction transformGizmoInteraction_;
    TransformGizmoModel transformGizmoModel_;
    TransformPivotManager transformPivotManager_;
    TransformGizmoManager transformGizmoManager_;
    TransformPanelViewModel transformPanelViewModel_;
    bool selectionBoxInteriorHovered_ = false;
    VoxelSelectionState voxelSelection_;
    VoxelToolState voxelToolState_;
    ToolManager toolManager_{voxelToolState_};
    ToolContext toolContext_{};
    SmartToolController smartToolController_{};
    SmartToolSession smartToolSession_{};
    SmartToolStroke smartToolStroke_{};
    SmartToolPlanPtr smartToolStrokePreviewPlan_;
    SmartToolExactPreviewMesh smartToolStrokePreviewMesh_{};
    std::uint64_t smartToolStrokePreviewPlanId_ = 0U;
    std::uint64_t smartToolStrokePreviewPlanRevision_ = 0U;
    std::uint64_t smartToolStrokePreviewStrokeRevision_ = 0U;
    // A Face drag is an extrusion of the single surface captured on MouseDown.
    // The current pointer can change depth, but never its source face or normal.
    std::optional<SmartToolFaceSeed> faceDepthLockedSeed_;
    std::optional<SmartToolFaceDepthDragAxis> faceDepthDragAxis_;
    int faceDepthLayers_ = 1;
    int faceDepthPlannedLayers_ = 0;
    std::optional<Asset::Voxel::VoxelPosition> smartLineLockedStart_;
    std::optional<Asset::Voxel::VoxelPosition> smartLinePlannedEnd_;
    SmartToolLineConstraintResolver smartToolLineConstraintResolver_;
    // A Line may keep its locked A while the pointer temporarily loses a
    // valid B. Such a suspended line must never commit its previous plan.
    bool smartLineEndpointValid_ = false;
    enum class SmartGeometryInteractionPhase : std::uint8_t
    {
        Base,
        Height
    };
    // Geometry locks its first point and full plane on MouseDown. Only B
    // changes during the base drag. Cylinder then freezes B and enters a
    // signed-height phase before the second click commits the exact plan.
    std::optional<SmartToolGeometryPlane> smartGeometryPlane_;
    std::optional<Asset::Voxel::VoxelPosition> smartGeometryPlannedEnd_;
    bool smartGeometryEndpointValid_ = false;
    SmartGeometryInteractionPhase smartGeometryPhase_ =
        SmartGeometryInteractionPhase::Base;
    SmartToolMode smartGeometryLockedMode_ = SmartToolMode::SingleVoxel;
    SmartAction smartGeometryLockedAction_ = SmartAction::Add;
    std::optional<SmartToolFaceDepthDragAxis> smartGeometryHeightDragAxis_;
    Vec2 smartGeometryHeightStartMouse_{};
    int smartGeometryHeight_ = 1;
    int smartGeometryPlannedHeight_ = 0;
    // Surface is not a Geometry alias. It locks an exposed source component
    // and a projection plane so its local brush extensions can remain on that
    // component while dragging through empty viewport space.
    std::optional<SmartToolFaceSeed> smartSurfaceLockedSeed_;
    std::optional<SmartToolGeometryPlane> smartSurfacePlane_;
    bool smartSurfaceEndpointValid_ = false;
    BrushProfileService brushProfileService_;
    SmartBrushSizeFeedback smartBrushSizeFeedback_{};
    bool smartBrushPreviewRefreshRequested_ = false;
    EditorInputService editorInputService_;
    VoxelToolInputController voxelToolInput_;
    VoxelToolInputController voxelToolSmokeInput_;
    VoxelBoxInteraction voxelBoxInteraction_;
    VoxelLineInteraction voxelLineInteraction_;
    VoxelSphereInteraction voxelSphereInteraction_;
    VoxelPlacementPreview voxelPlacementPreview_;
    // Current frame target for the universal screen-space cursor overlay.
    std::optional<UniversalCursor2DTarget> universalCursor2DTarget_;
    const Asset::Voxel::VoxelDocument* pencilPreviewDocument_ = nullptr;
    std::uint64_t pencilPreviewRevision_ = 0U;
    std::uint64_t pencilPreviewGeneration_ = 0U;
    std::optional<Asset::Voxel::VoxelPosition> pencilPreviewAnchor_;
    VoxelHitFace pencilPreviewFace_ = VoxelHitFace::None;
    std::size_t pencilPreviewHitSubModelIndex_ = 0U;
    SmartBrushState pencilPreviewState_{};
    bool pencilPreviewUsesWorkplane_ = false;
    bool pencilPreviewCacheValid_ = false;
    SmartPreviewCache smartPreviewCache_{};
    SmartToolExactPreviewCache smartToolExactPreviewCache_{};
    const SmartPreviewData* smartBrushGhostPreview_ = nullptr;
    VoxelPaintBrushEvaluation paintPreviewEvaluation_;
    const Asset::Voxel::VoxelDocument* paintPreviewDocument_ = nullptr;
    std::uint64_t paintPreviewRevision_ = 0U;
    std::uint64_t paintPreviewGeneration_ = 0U;
    std::optional<VoxelCoordinates> paintPreviewCoordinates_;
    VoxelHitFace paintPreviewFace_ = VoxelHitFace::None;
    std::size_t paintPreviewHitSubModelIndex_ = 0U;
    SmartBrushState paintPreviewState_{};
    bool paintPreviewCacheValid_ = false;
    std::optional<WorkplaneHit> workplaneHit_;
    VoxelEditHistory voxelEditHistory_;
    std::optional<VoxelToolResult> lastVoxelToolResult_;
    std::optional<VoxelEraserResult> lastVoxelEraserResult_;
    std::optional<VoxelPaintBrushResult> lastVoxelPaintBrushResult_;
    std::optional<VoxelFillResult> lastVoxelFillResult_;
    std::optional<VoxelBoxResult> lastVoxelBoxResult_;
    std::optional<VoxelLineResult> lastVoxelLineResult_;
    std::optional<VoxelSphereResult> lastVoxelSphereResult_;
    std::string voxelMoveStatusMessage_;
    std::string voxelDuplicateStatusMessage_;
    VoxelRotationDirection voxelRotateDirection_ =
        VoxelRotationDirection::Clockwise;
    VoxelRotationAxis voxelRotateAxis_ = VoxelRotationAxis::Y;
    std::int32_t voxelRotateQuarterTurns_ = 1;
    std::string voxelRotateStatusMessage_;
    VoxelMirrorAxis voxelMirrorAxis_ = VoxelMirrorAxis::X;
    std::string voxelMirrorStatusMessage_;
    VoxelScaleMode voxelScaleMode_ = VoxelScaleMode::X;
    std::optional<Asset::Voxel::VoxelDimensions> voxelScaleTargetDimensions_;
    std::string voxelScaleStatusMessage_;
    VoxelAlignDirection voxelAlignDirection_ = VoxelAlignDirection::Left;
    std::string voxelAlignStatusMessage_;
    std::string transformPanelStatusMessage_;
    PaintPaletteSelection paintPaletteSelection_;
    // Commands are scoped to the current project/model session. Clearing the
    // history before replacement prevents future commands from retaining a
    // handle to an obsolete model.
    CommandHistory commandHistory_;
    std::uint64_t voxelModelGeneration_ = 0U;
    Vec3 voxelModelCenter_{};
    EditorConsoleService console_;
    EditorCloseRequest closeRequest_{};
    EditorExitRequest exitRequest_{};
    std::unique_ptr<FileDialogService> fileDialogService_;
    std::unique_ptr<ProjectFolderOpener> projectFolderOpener_;
    ProjectDialogPreferences projectDialogPreferences_;
    ProjectSessionService projectSessionService_;
    WindowsProjectRecycleBin projectRecycleBin_;
    ProjectDeletionService projectDeletionService_{projectRecycleBin_};
    DirtyActionConfirmation dirtyActionConfirmation_;
    std::optional<DestructiveAction> deferredDirtyAction_;
    bool deferredDirtySaveRequested_ = false;
    std::filesystem::path pendingProjectPath_;
    std::filesystem::path pendingVoxelPath_;
    VoxelModelCreationRequest pendingVoxelModelCreation_{};
    VoxelModelCreationCollisionAction pendingVoxelModelCollisionAction_ =
        VoxelModelCreationCollisionAction::Ask;
    bool pendingInstantVoxelModelCreation_ = false;
    bool pendingRecentProject_ = false;

    std::array<char, 128> newProjectName_{};
    std::array<char, 1024> newProjectParentPath_{};
    std::array<char, 1024> openProjectFilePath_{};
    std::array<char, 128> newVoxelModelName_{};
    std::array<int, 3> newVoxelModelDimensions_{64, 64, 64};
    std::string voxelModelCreationError_;
    std::string projectDialogError_;
    std::string welcomeError_;
    std::string welcomeNotification_;
    std::string projectDeletionError_;
    std::optional<std::filesystem::path> failedRecentProjectPath_;
    std::filesystem::path pendingProjectDeletionPath_;
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

    bool showTools_ = true;
    bool showToolOptions_ = true;
    bool showExplorer_ = true;
    bool showScene_ = true;
    bool showInspector_ = true;
    bool showTransformPanel_ = true;
    bool showPalette_ = true;
    bool showAssetBrowser_ = true;
    bool showForgeLibrary_ = true;
    bool showProfiler_ = false;
    bool showImGuiDemo_ = false;
    bool useViewportInteractionV2_ = false;
    bool usePencilViewportInteractionV2_ = false;
    std::uint64_t pencilV2RenderedPresentationRevision_ = 0U;
    std::vector<Asset::Voxel::VoxelPosition> pencilV2PreviewPositions_;
    bool showAboutPopup_ = false;
    bool showNewProjectPopup_ = false;
    bool showOpenProjectPopup_ = false;
    bool showNewVoxelModelPopup_ = false;
    bool showVoxelModelCollisionPopup_ = false;
    bool showImportConfirmationPopup_ = false;
    bool showImportCollisionPopup_ = false;
    bool showOpenImportedModelPopup_ = false;
    bool showDirtyConfirmationPopup_ = false;
    bool showProjectDeletionPopup_ = false;
    bool resetLayoutRequested_ = false;
    bool createWorkspaceSettingsChecked_ = false;
    bool createWorkspaceMigrationApplied_ = false;
    bool thumbnailVisualLayoutRequested_ = false;
    bool thumbnailVisualMode_ = false;
    bool voxelViewportRendered_ = false;
    bool voxelViewportRenderFailed_ = false;
    bool createWorkspaceSmokeSeedLegacy_ = false;
    bool createWorkspaceSmokeMigrated_ = false;
    bool createWorkspaceSmokeReset_ = false;
    bool createWorkspaceSmokePassed_ = false;
    bool voxelSelectionClickCandidate_ = false;
    std::optional<Asset::Voxel::VoxelPosition> selectionPointerAnchor_;
    SelectionMode selectionPointerMode_ = SelectionMode::Replace;
    bool selectionSystemSmokeStarted_ = false;
    bool selectionSystemSmokeToolChanged_ = false;
    bool selectionSystemSmokePassed_ = false;
    std::vector<std::pair<Asset::Voxel::VoxelPosition, Asset::Voxel::Voxel>>
        transformPreviewSmokeDocumentSnapshot_;
    std::uint64_t transformPreviewSmokeDocumentRevision_ = 0U;
    std::size_t transformPreviewSmokeUndoCount_ = 0U;
    std::size_t transformPreviewSmokeRedoCount_ = 0U;
    std::size_t transformPreviewSmokeHighlightUploadBaseline_ = 0U;
    bool transformPreviewSmokeDocumentDirty_ = false;
    bool transformPreviewSmokeRendered_ = false;
    bool voxelEditInProgress_ = false;
    StampPreviewController stampPreview_;
    bool viewportFocusRequested_ = false;
    bool viewportFocusApplied_ = false;
    std::optional<std::size_t> paletteColorEditorIndex_;
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
    bool layoutStabilitySmokeTransformDocked_ = false;
    bool layoutStabilitySmokeReadyForShutdown_ = false;
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
    bool directCreationSmokeUndone_ = false;
    bool directCreationSmokeRedone_ = false;
    bool directCreationSmokeSaved_ = false;
    bool directCreationSmokeCleaned_ = false;
    std::filesystem::path paletteSmokeProjectFile_;
    std::filesystem::path paletteSmokeModelPath_;
    Asset::Voxel::VoxelPosition paletteSmokeTarget_{};
    Asset::Voxel::VoxelColor paletteSmokeColor_{};
    std::size_t paletteSmokeIndex_ = 42U;
    bool paletteSmokeLayoutValid_ = false;
    bool paletteSmokeCreated_ = false;
    bool paletteSmokePencilled_ = false;
    bool paletteSmokeSavedAndClosed_ = false;
    bool paletteSmokeRestored_ = false;
    bool paletteSmokeCleaned_ = false;
    std::filesystem::path voxelFillSmokePath_;
    bool voxelFillSmokeSeeded_ = false;
    bool voxelFillSmokeApplied_ = false;
    bool voxelFillSmokeUndone_ = false;
    bool voxelFillSmokeRedone_ = false;
    bool voxelFillSmokeSaved_ = false;
    bool voxelFillSmokeCleaned_ = false;
    std::filesystem::path voxelBoxSmokePath_;
    bool voxelBoxSmokeCreated_ = false;
    bool voxelBoxSmokeApplied_ = false;
    bool voxelBoxSmokeUndone_ = false;
    bool voxelBoxSmokeRedone_ = false;
    bool voxelBoxSmokeSaved_ = false;
    bool voxelBoxSmokeCleaned_ = false;
    std::filesystem::path voxelLineSmokePath_;
    bool voxelLineSmokeCreated_ = false;
    bool voxelLineSmokeApplied_ = false;
    bool voxelLineSmokeUndone_ = false;
    bool voxelLineSmokeRedone_ = false;
    bool voxelLineSmokeSaved_ = false;
    bool voxelLineSmokeCleaned_ = false;
    std::filesystem::path voxelSphereSmokePath_;
    bool voxelSphereSmokeCreated_ = false;
    bool voxelSphereSmokeApplied_ = false;
    bool voxelSphereSmokeUndone_ = false;
    bool voxelSphereSmokeRedone_ = false;
    bool voxelSphereSmokeSaved_ = false;
    bool voxelSphereSmokeCleaned_ = false;
    std::filesystem::path modernToolbarSmokePath_;
    bool modernToolbarSmokeDisabled_ = false;
    bool modernToolbarSmokeToolsEnabled_ = false;
    bool modernToolbarSmokeSingleActive_ = false;
    bool modernToolbarSmokeSaved_ = false;
    bool modernToolbarSmokeCleaned_ = false;
    std::filesystem::path keyboardShortcutsSmokePath_;
    bool keyboardShortcutsSmokeTools_ = false;
    bool keyboardShortcutsSmokeEdited_ = false;
    bool keyboardShortcutsSmokeSaved_ = false;
    bool keyboardShortcutsSmokeUndone_ = false;
    bool keyboardShortcutsSmokeRedone_ = false;
    bool keyboardShortcutsSmokeCancelled_ = false;
    bool keyboardShortcutsSmokeCleaned_ = false;
    std::filesystem::path voxelMoveSmokePath_;
    bool voxelMoveSmokePrepared_ = false;
    bool voxelMoveSmokeApplied_ = false;
    bool voxelMoveSmokeUndone_ = false;
    bool voxelMoveSmokeRedone_ = false;
    bool voxelMoveSmokeRejected_ = false;
    bool voxelMoveSmokeSaved_ = false;
    bool voxelMoveSmokeReopened_ = false;
    bool voxelMoveSmokeCleaned_ = false;
    std::filesystem::path voxelDuplicateSmokePath_;
    bool voxelDuplicateSmokePrepared_ = false;
    bool voxelDuplicateSmokeApplied_ = false;
    bool voxelDuplicateSmokeUndone_ = false;
    bool voxelDuplicateSmokeRedone_ = false;
    bool voxelDuplicateSmokeRepeated_ = false;
    bool voxelDuplicateSmokeRejected_ = false;
    bool voxelDuplicateSmokeSaved_ = false;
    bool voxelDuplicateSmokeReopened_ = false;
    bool voxelDuplicateSmokeCleaned_ = false;
    std::filesystem::path voxelRotateSmokePath_;
    bool voxelRotateSmokePrepared_ = false;
    bool voxelRotateSmokeApplied_ = false;
    bool voxelRotateSmokeUndoRedo_ = false;
    bool voxelRotateSmokeCycled_ = false;
    bool voxelRotateSmokeSaved_ = false;
    bool voxelRotateSmokeReopened_ = false;
    bool voxelRotateSmokeCleaned_ = false;
    std::filesystem::path voxelMirrorSmokePath_;
    bool voxelMirrorSmokePrepared_ = false;
    bool voxelMirrorSmokePreviewed_ = false;
    bool voxelMirrorSmokeApplied_ = false;
    bool voxelMirrorSmokeUndoRedo_ = false;
    bool voxelMirrorSmokeInvolutive_ = false;
    bool voxelMirrorSmokeIdentity_ = false;
    bool voxelMirrorSmokeRejected_ = false;
    bool voxelMirrorSmokeSaved_ = false;
    bool voxelMirrorSmokeReopened_ = false;
    bool voxelMirrorSmokeCleaned_ = false;
    std::filesystem::path voxelScaleSmokePath_;
    bool voxelScaleSmokePrepared_ = false;
    bool voxelScaleSmokePreviewed_ = false;
    bool voxelScaleSmokeApplied_ = false;
    bool voxelScaleSmokeUndoRedo_ = false;
    bool voxelScaleSmokeRejected_ = false;
    bool voxelScaleSmokeSaved_ = false;
    bool voxelScaleSmokeReopened_ = false;
    bool voxelScaleSmokeCleaned_ = false;
    std::filesystem::path voxelAlignSmokePath_;
    bool voxelAlignSmokePrepared_ = false;
    bool voxelAlignSmokeDirections_ = false;
    bool voxelAlignSmokeApplied_ = false;
    bool voxelAlignSmokeUndoRedo_ = false;
    bool voxelAlignSmokeRejected_ = false;
    bool voxelAlignSmokeSaved_ = false;
    bool voxelAlignSmokeReopened_ = false;
    bool voxelAlignSmokeSaveOnExit_ = false;
    std::filesystem::path transformGizmoSmokePath_;
    float transformGizmoSmokeInitialLength_ = 0.0F;
    float transformGizmoSmokeInitialPixels_ = 0.0F;
    bool transformGizmoSmokePrepared_ = false;
    bool transformGizmoSmokeRendered_ = false;
    bool transformGizmoSmokeScaleStable_ = false;
    bool transformGizmoSmokeModes_ = false;
    bool transformGizmoSmokeVisibility_ = false;
    bool transformGizmoSmokeMoveUndoRedo_ = false;
    bool transformGizmoSmokeDocumentReset_ = false;
    bool transformGizmoSmokeSaveOnExit_ = false;
    std::filesystem::path moveGizmoSmokePath_;
    bool moveGizmoSmokePrepared_ = false;
    bool moveGizmoSmokeAxes_ = false;
    bool moveGizmoSmokeMoved_ = false;
    bool moveGizmoSmokeUndoRedo_ = false;
    bool moveGizmoSmokeRejected_ = false;
    bool moveGizmoSmokeCancelled_ = false;
    bool moveGizmoSmokeCleaned_ = false;
    std::filesystem::path rotateGizmoSmokePath_;
    bool rotateGizmoSmokePrepared_ = false;
    bool rotateGizmoSmokeInteractive_ = false;
    bool rotateGizmoSmokeApplied_ = false;
    bool rotateGizmoSmokeUndoRedo_ = false;
    bool rotateGizmoSmokeCancelled_ = false;
    bool rotateGizmoSmokeSaved_ = false;
    bool rotateGizmoSmokeReopened_ = false;
    std::filesystem::path scaleGizmoSmokePath_;
    bool scaleGizmoSmokePrepared_ = false;
    bool scaleGizmoSmokeInteractive_ = false;
    bool scaleGizmoSmokeApplied_ = false;
    bool scaleGizmoSmokeUndoRedo_ = false;
    bool scaleGizmoSmokeRejected_ = false;
    bool scaleGizmoSmokeCleaned_ = false;
    std::filesystem::path transformGizmoManagerSmokePath_;
    bool transformGizmoManagerSmokePrepared_ = false;
    bool transformGizmoManagerSmokeModes_ = false;
    bool transformGizmoManagerSmokeTransitions_ = false;
    bool transformGizmoManagerSmokeInvalidation_ = false;
    bool transformGizmoManagerSmokeCleaned_ = false;
    std::filesystem::path saveOnExitSmokePath_;
    bool saveOnExitSmokeRequested_ = false;
    bool saveOnExitSmokeCallbackDeferred_ = false;
    bool saveOnExitSmokePassed_ = false;
    std::filesystem::path transformPanelSmokePath_;
    bool transformPanelSmokePrepared_ = false;
    bool transformPanelSmokeMoved_ = false;
    bool transformPanelSmokeRotated_ = false;
    bool transformPanelSmokeScaled_ = false;
    bool transformPanelSmokePivot_ = false;
    bool transformPanelSmokeUndoRedo_ = false;
    bool transformPanelSmokeCleaned_ = false;
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
