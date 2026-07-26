#include "EditorLayer.h"

#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Core/Event/EventDispatcher.h"
#include "VoxelForge/Core/Event/FileDropEvent.h"
#include "VoxelForge/Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string>
#include <stdexcept>
#include <utility>

namespace VoxelForge::Editor
{

EditorLayer::EditorLayer(
    Project::ProjectManager& projectManager,
    WindowTitleCallback windowTitleCallback,
    ApplicationCloseCallback applicationCloseCallback,
    const std::size_t smokeTestFrameLimit,
    std::filesystem::path startupVoxPath,
    const bool requireVoxelViewportRender,
    const bool voxelSelectionSmokeTest,
    const bool voxelSelectionVisualTest,
    const bool eraseVoxelSmokeTest,
    const bool eraseVoxelVisualTest,
    const bool paintVoxelSmokeTest,
    const bool paintVoxelVisualTest,
    const bool voxelSaveSmokeTest,
    const bool addVoxelSmokeTest,
    const bool modelImportSmokeTest,
    const bool modelImportVisualTest,
    const bool qualityOfLifeSmokeTest,
    std::filesystem::path qualityOfLifeParent,
    const bool dragDropImportSmokeTest,
    std::vector<std::filesystem::path> dragDropSmokePaths,
    const bool voxelDocumentSmokeTest,
    const bool voxelRenderSyncSmokeTest,
    const bool voxelRayPickingSmokeTest,
    const bool voxelPencilSmokeTest,
    const bool voxelEraserSmokeTest,
    const bool voxelUndoRedoSmokeTest,
    const bool firstCreationExperienceSmokeTest,
    const bool layoutStabilitySmokeTest,
    const bool doubleClickCameraSmokeTest,
    const bool persistentWorkplaneSmokeTest,
    const bool projectSessionRestoreSmokeTest,
    const bool directCreationFlowSmokeTest,
    const bool paletteUiSmokeTest,
    const bool voxelFillSmokeTest,
    const bool voxelBoxSmokeTest,
    const bool voxelLineSmokeTest,
    const bool voxelSphereSmokeTest,
    const bool modernToolbarSmokeTest,
    const bool keyboardShortcutsSmokeTest,
    const bool voxelMoveSmokeTest,
    const bool voxelDuplicateSmokeTest,
    const bool voxelRotateSmokeTest,
    const bool voxelMirrorSmokeTest,
    const bool voxelScaleSmokeTest,
    const bool voxelAlignSmokeTest,
    const bool moveGizmoSmokeTest,
    const bool rotateGizmoSmokeTest,
    const bool scaleGizmoSmokeTest,
    const bool transformGizmoManagerSmokeTest,
    const bool transformGizmoFoundationSmokeTest,
    const bool transformPanelSmokeTest,
    const bool saveOnExitSmokeTest,
    std::filesystem::path imguiIniPathOverride,
    const bool createWorkspaceSmokeTest,
    const bool stampLivePreviewVisualTest,
    const bool stampPlacementVisualTest)
    : Layer("VoxelForge Editor Layer"),
      layoutPersistence_(std::move(imguiIniPathOverride)),
      workspace_(
          projectManager,
          std::move(windowTitleCallback),
          qualityOfLifeSmokeTest || modelImportSmokeTest || modelImportVisualTest ||
              dragDropImportSmokeTest || voxelDocumentSmokeTest ||
              voxelRenderSyncSmokeTest || voxelRayPickingSmokeTest ||
              voxelPencilSmokeTest || voxelEraserSmokeTest ||
              voxelUndoRedoSmokeTest || firstCreationExperienceSmokeTest ||
              layoutStabilitySmokeTest || doubleClickCameraSmokeTest ||
              persistentWorkplaneSmokeTest || projectSessionRestoreSmokeTest ||
              directCreationFlowSmokeTest || paletteUiSmokeTest ||
              voxelFillSmokeTest || voxelBoxSmokeTest || voxelLineSmokeTest ||
              voxelSphereSmokeTest || modernToolbarSmokeTest ||
              keyboardShortcutsSmokeTest || voxelMoveSmokeTest ||
              voxelDuplicateSmokeTest || voxelRotateSmokeTest ||
              voxelMirrorSmokeTest || voxelScaleSmokeTest ||
              voxelAlignSmokeTest || moveGizmoSmokeTest ||
              rotateGizmoSmokeTest || scaleGizmoSmokeTest ||
              transformGizmoManagerSmokeTest ||
              transformGizmoFoundationSmokeTest ||
              transformPanelSmokeTest ||
              saveOnExitSmokeTest || stampLivePreviewVisualTest ||
              stampPlacementVisualTest
              ? (qualityOfLifeSmokeTest
                  ? qualityOfLifeParent
                  : startupVoxPath.parent_path()) / "preferences.ini"
              : ProjectDialogPreferences::DefaultStorageFilePath(),
          qualityOfLifeSmokeTest || modelImportSmokeTest || modelImportVisualTest ||
              dragDropImportSmokeTest || voxelDocumentSmokeTest ||
              voxelRenderSyncSmokeTest || voxelRayPickingSmokeTest ||
              voxelPencilSmokeTest || voxelEraserSmokeTest ||
              voxelUndoRedoSmokeTest || firstCreationExperienceSmokeTest ||
              layoutStabilitySmokeTest || doubleClickCameraSmokeTest ||
              persistentWorkplaneSmokeTest || projectSessionRestoreSmokeTest ||
              directCreationFlowSmokeTest || paletteUiSmokeTest ||
              voxelFillSmokeTest || voxelBoxSmokeTest || voxelLineSmokeTest ||
              voxelSphereSmokeTest || modernToolbarSmokeTest ||
              keyboardShortcutsSmokeTest || voxelMoveSmokeTest ||
              voxelDuplicateSmokeTest || voxelRotateSmokeTest ||
              voxelMirrorSmokeTest || voxelScaleSmokeTest ||
              voxelAlignSmokeTest || moveGizmoSmokeTest ||
              rotateGizmoSmokeTest || scaleGizmoSmokeTest ||
              transformGizmoManagerSmokeTest ||
              transformGizmoFoundationSmokeTest ||
              transformPanelSmokeTest ||
              saveOnExitSmokeTest || stampLivePreviewVisualTest ||
              stampPlacementVisualTest),
      applicationCloseCallback_(std::move(applicationCloseCallback)),
      smokeTestFrameLimit_(smokeTestFrameLimit),
      startupVoxPath_(std::move(startupVoxPath)),
      requireVoxelViewportRender_(requireVoxelViewportRender),
      voxelSelectionSmokeTest_(voxelSelectionSmokeTest),
      voxelSelectionVisualTest_(voxelSelectionVisualTest),
      eraseVoxelSmokeTest_(eraseVoxelSmokeTest),
      eraseVoxelVisualTest_(eraseVoxelVisualTest),
      paintVoxelSmokeTest_(paintVoxelSmokeTest),
      paintVoxelVisualTest_(paintVoxelVisualTest),
      voxelSaveSmokeTest_(voxelSaveSmokeTest),
      addVoxelSmokeTest_(addVoxelSmokeTest),
      modelImportSmokeTest_(modelImportSmokeTest),
      modelImportVisualTest_(modelImportVisualTest),
      qualityOfLifeSmokeTest_(qualityOfLifeSmokeTest),
      qualityOfLifeParent_(std::move(qualityOfLifeParent)),
      dragDropImportSmokeTest_(dragDropImportSmokeTest),
      dragDropSmokePaths_(std::move(dragDropSmokePaths)),
      voxelDocumentSmokeTest_(voxelDocumentSmokeTest),
      voxelRenderSyncSmokeTest_(voxelRenderSyncSmokeTest),
      voxelRayPickingSmokeTest_(voxelRayPickingSmokeTest),
      voxelPencilSmokeTest_(voxelPencilSmokeTest),
      voxelEraserSmokeTest_(voxelEraserSmokeTest),
      voxelUndoRedoSmokeTest_(voxelUndoRedoSmokeTest),
      firstCreationExperienceSmokeTest_(firstCreationExperienceSmokeTest),
      layoutStabilitySmokeTest_(layoutStabilitySmokeTest),
      doubleClickCameraSmokeTest_(doubleClickCameraSmokeTest),
      persistentWorkplaneSmokeTest_(persistentWorkplaneSmokeTest),
      projectSessionRestoreSmokeTest_(projectSessionRestoreSmokeTest),
      directCreationFlowSmokeTest_(directCreationFlowSmokeTest),
      paletteUiSmokeTest_(paletteUiSmokeTest),
      voxelFillSmokeTest_(voxelFillSmokeTest),
      voxelBoxSmokeTest_(voxelBoxSmokeTest),
      voxelLineSmokeTest_(voxelLineSmokeTest),
      voxelSphereSmokeTest_(voxelSphereSmokeTest),
      modernToolbarSmokeTest_(modernToolbarSmokeTest),
      keyboardShortcutsSmokeTest_(keyboardShortcutsSmokeTest),
      voxelMoveSmokeTest_(voxelMoveSmokeTest),
      voxelDuplicateSmokeTest_(voxelDuplicateSmokeTest),
      voxelRotateSmokeTest_(voxelRotateSmokeTest),
      voxelMirrorSmokeTest_(voxelMirrorSmokeTest),
      voxelScaleSmokeTest_(voxelScaleSmokeTest),
      voxelAlignSmokeTest_(voxelAlignSmokeTest),
      moveGizmoSmokeTest_(moveGizmoSmokeTest),
      rotateGizmoSmokeTest_(rotateGizmoSmokeTest),
      scaleGizmoSmokeTest_(scaleGizmoSmokeTest),
      transformGizmoManagerSmokeTest_(transformGizmoManagerSmokeTest),
      transformGizmoFoundationSmokeTest_(transformGizmoFoundationSmokeTest),
      transformPanelSmokeTest_(transformPanelSmokeTest),
      saveOnExitSmokeTest_(saveOnExitSmokeTest),
      createWorkspaceSmokeTest_(createWorkspaceSmokeTest),
      stampLivePreviewVisualTest_(stampLivePreviewVisualTest),
      stampPlacementVisualTest_(stampPlacementVisualTest)
{
    if (voxelDocumentSmokeTest_)
        voxelDocumentSmokeSourcePath_ = startupVoxPath_;
    if (voxelRenderSyncSmokeTest_)
        voxelRenderSyncSmokeSourcePath_ = startupVoxPath_;
    if (voxelRayPickingSmokeTest_)
        voxelRayPickingSmokeSourcePath_ = startupVoxPath_;
    if (voxelPencilSmokeTest_)
        voxelPencilSmokeSourcePath_ = startupVoxPath_;
    if (voxelEraserSmokeTest_)
        voxelEraserSmokeSourcePath_ = startupVoxPath_;
    if (voxelUndoRedoSmokeTest_)
        voxelUndoRedoSmokeSourcePath_ = startupVoxPath_;
}

void EditorLayer::OnAttach()
{
    CreateDefaultScene();
    Core::Logger::Instance().Info("Viewport 3D prototype attached.");
}

void EditorLayer::OnDetach()
{
    if (layoutPersistence_.IsInitialized() &&
        ImGui::GetCurrentContext() != nullptr)
    {
        ImGui::SaveIniSettingsToDisk(layoutPersistence_.IniFilename());
        ImGui::GetIO().IniFilename = nullptr;
    }
    selectedEntity_ = nullptr;
    scene_.reset();
    Core::Logger::Instance().Info("Viewport 3D prototype detached.");
}

void EditorLayer::OnUpdate()
{
}

void EditorLayer::OnEvent(VoxelForge::Event& event)
{
    VoxelForge::EventDispatcher dispatcher(event);
    dispatcher.Dispatch<VoxelForge::FileDropBeginEvent>(
        [this](VoxelForge::FileDropBeginEvent& drop)
        {
            workspace_.BeginFileDrop(drop.GetX(), drop.GetY());
            return true;
        });
    dispatcher.Dispatch<VoxelForge::FileDropPositionEvent>(
        [this](VoxelForge::FileDropPositionEvent& drop)
        {
            workspace_.UpdateFileDropPosition(drop.GetX(), drop.GetY());
            return true;
        });
    dispatcher.Dispatch<VoxelForge::FileDropFileEvent>(
        [this](VoxelForge::FileDropFileEvent& drop)
        {
            workspace_.AddDroppedFile(
                drop.GetPath(), drop.GetX(), drop.GetY());
            return true;
        });
    dispatcher.Dispatch<VoxelForge::FileDropCompleteEvent>(
        [this](VoxelForge::FileDropCompleteEvent& drop)
        {
            workspace_.CompleteFileDrop(drop.GetX(), drop.GetY());
            return true;
        });
}

void EditorLayer::CreateDefaultScene()
{
    scene_ = std::make_unique<Scene::Scene>("DemoScene");

    Scene::Entity& camera = scene_->CreateEntity("Camera");
    camera.GetMetadata().Category = "Camera";
    camera.GetForgeDNA().Purpose = "Editor View";
    camera.GetTransform().Position = {8.0F, 6.0F, 10.0F};

    Scene::Entity& light = scene_->CreateEntity("Directional Light");
    light.GetMetadata().Category = "Light";
    light.GetForgeDNA().Purpose = "Lighting";
    light.GetTransform().Rotation = {45.0F, -30.0F, 0.0F};

    Scene::Entity& voxelObject = scene_->CreateEntity("Voxel Object");
    voxelObject.GetMetadata().Category = "Voxel";
    voxelObject.GetMetadata().Tags = {"Destructible", "Prototype"};
    voxelObject.GetForgeDNA().Style = "VoxelForge Default";
    voxelObject.GetForgeDNA().Palette = "Forge Neutral";
    voxelObject.GetForgeDNA().Purpose = "Creation";
}

void EditorLayer::OnImGuiRender()
{
    if (!layoutPersistence_.IsInitialized())
    {
        if (!layoutPersistence_.Initialize())
        {
            throw std::runtime_error(
                "ImGui layout initialization failed: " +
                layoutPersistence_.LastError());
        }
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = layoutPersistence_.IniFilename();
        ImGui::ClearIniSettings();
        if (std::filesystem::is_regular_file(layoutPersistence_.Path()))
        {
            ImGui::LoadIniSettingsFromDisk(layoutPersistence_.IniFilename());
        }
    }
    if (!startupVoxPath_.empty() && !modelImportSmokeTest_ &&
        !modelImportVisualTest_ && !dragDropImportSmokeTest_ &&
        !firstCreationExperienceSmokeTest_ && !layoutStabilitySmokeTest_ &&
        !persistentWorkplaneSmokeTest_ && !projectSessionRestoreSmokeTest_ &&
        !directCreationFlowSmokeTest_ && !paletteUiSmokeTest_ &&
        !voxelFillSmokeTest_ && !voxelBoxSmokeTest_ && !voxelLineSmokeTest_ &&
        !voxelSphereSmokeTest_ && !modernToolbarSmokeTest_ &&
         !keyboardShortcutsSmokeTest_ && !voxelMoveSmokeTest_ &&
         !voxelDuplicateSmokeTest_ && !voxelRotateSmokeTest_ &&
         !voxelMirrorSmokeTest_ && !voxelScaleSmokeTest_ &&
         !voxelAlignSmokeTest_ && !moveGizmoSmokeTest_ &&
         !rotateGizmoSmokeTest_ && !scaleGizmoSmokeTest_ &&
         !transformGizmoManagerSmokeTest_ &&
         !transformGizmoFoundationSmokeTest_ && !transformPanelSmokeTest_ &&
         !saveOnExitSmokeTest_)
    {
        if (!workspace_.OpenVoxInViewport(startupVoxPath_))
        {
            throw std::runtime_error("Viewport startup model failed to load.");
        }
        startupVoxPath_.clear();
    }
    if (requireVoxelViewportRender_ && renderedFrameCount_ == 5U)
        workspace_.SetVoxelViewportView(EditorCameraView::Front);
    if (requireVoxelViewportRender_ && renderedFrameCount_ == 15U)
        workspace_.SetVoxelViewportView(EditorCameraView::Top);
    if (voxelSelectionSmokeTest_ ||
        (voxelSelectionVisualTest_ &&
         (renderedFrameCount_ == 0U || renderedFrameCount_ == 10U)))
    {
        voxelSelectionRayHit_ |=
            workspace_.RunVoxelSelectionSmokeStep(renderedFrameCount_);
    }
    if (eraseVoxelSmokeTest_ ||
        (eraseVoxelVisualTest_ && renderedFrameCount_ <= 1U))
    {
        static_cast<void>(
            workspace_.RunEraseVoxelSmokeStep(renderedFrameCount_));
    }
    if (paintVoxelSmokeTest_ ||
        (paintVoxelVisualTest_ && renderedFrameCount_ <= 1U))
    {
        static_cast<void>(
            workspace_.RunPaintVoxelSmokeStep(renderedFrameCount_));
    }
    if (voxelSaveSmokeTest_)
    {
        static_cast<void>(
            workspace_.RunVoxelSaveSmokeStep(renderedFrameCount_));
    }
    if (addVoxelSmokeTest_)
    {
        static_cast<void>(
            workspace_.RunAddVoxelSmokeStep(renderedFrameCount_));
    }
    if (modelImportSmokeTest_ || modelImportVisualTest_)
    {
        static_cast<void>(workspace_.RunModelImportSmokeStep(
            renderedFrameCount_, startupVoxPath_, modelImportVisualTest_));
    }
    if (qualityOfLifeSmokeTest_)
    {
        static_cast<void>(workspace_.RunQualityOfLifeSmokeStep(
            renderedFrameCount_, qualityOfLifeParent_));
    }
    if (dragDropImportSmokeTest_)
    {
        static_cast<void>(workspace_.RunDragDropImportSmokeStep(
            renderedFrameCount_, dragDropSmokePaths_));
    }
    if (voxelDocumentSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelDocumentSmokeStep(
            renderedFrameCount_, voxelDocumentSmokeSourcePath_));
    }
    if (voxelRenderSyncSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelRenderSyncSmokeStep(
            renderedFrameCount_, voxelRenderSyncSmokeSourcePath_));
    }
    if (firstCreationExperienceSmokeTest_)
    {
        static_cast<void>(workspace_.RunFirstCreationExperienceSmokeStep(
            renderedFrameCount_));
    }
    if (persistentWorkplaneSmokeTest_)
    {
        static_cast<void>(workspace_.RunPersistentWorkplaneSmokeStep(
            renderedFrameCount_));
    }
    if (projectSessionRestoreSmokeTest_)
    {
        static_cast<void>(workspace_.RunProjectSessionRestoreSmokeStep(
            renderedFrameCount_));
    }
    if (directCreationFlowSmokeTest_)
    {
        static_cast<void>(workspace_.RunDirectCreationFlowSmokeStep(
            renderedFrameCount_));
    }
    if (voxelFillSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelFillSmokeStep(
            renderedFrameCount_));
    }
    if (voxelBoxSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelBoxSmokeStep(
            renderedFrameCount_));
    }
    if (voxelLineSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelLineSmokeStep(
            renderedFrameCount_));
    }
    if (voxelSphereSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelSphereSmokeStep(
            renderedFrameCount_));
    }
    if (modernToolbarSmokeTest_)
    {
        static_cast<void>(workspace_.RunModernToolbarSmokeStep(
            renderedFrameCount_));
    }
    if (keyboardShortcutsSmokeTest_)
    {
        static_cast<void>(workspace_.RunKeyboardShortcutsSmokeStep(
            renderedFrameCount_));
    }
    if (voxelMoveSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelMoveSmokeStep(
            renderedFrameCount_));
    }
    if (voxelDuplicateSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelDuplicateSmokeStep(
            renderedFrameCount_));
    }
    if (voxelRotateSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelRotateSmokeStep(
            renderedFrameCount_));
    }
    if (voxelMirrorSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelMirrorSmokeStep(
            renderedFrameCount_));
    }
    if (voxelScaleSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelScaleSmokeStep(
            renderedFrameCount_));
    }
    if (voxelAlignSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelAlignSmokeStep(
            renderedFrameCount_));
    }
    if (moveGizmoSmokeTest_)
    {
        static_cast<void>(workspace_.RunMoveGizmoSmokeStep(
            renderedFrameCount_));
    }
    if (rotateGizmoSmokeTest_)
    {
        static_cast<void>(workspace_.RunRotateGizmoSmokeStep(
            renderedFrameCount_));
    }
    if (scaleGizmoSmokeTest_)
    {
        static_cast<void>(workspace_.RunScaleGizmoSmokeStep(
            renderedFrameCount_));
    }
    if (transformGizmoManagerSmokeTest_)
    {
        static_cast<void>(workspace_.RunTransformGizmoManagerSmokeStep(
            renderedFrameCount_));
    }
    if (transformGizmoFoundationSmokeTest_)
    {
        static_cast<void>(workspace_.RunTransformGizmoFoundationSmokeStep(
            renderedFrameCount_));
    }
    if (transformPanelSmokeTest_)
    {
        static_cast<void>(workspace_.RunTransformPanelSmokeStep(
            renderedFrameCount_));
    }
    if (saveOnExitSmokeTest_)
    {
        static_cast<void>(workspace_.RunSaveOnExitSmokeStep(
            renderedFrameCount_));
    }
    if (stampLivePreviewVisualTest_)
    {
        static_cast<void>(workspace_.RunStampLivePreviewVisualStep(
            renderedFrameCount_));
    }
    if (stampPlacementVisualTest_ &&
        workspace_.RunStampPlacementVisualStep(renderedFrameCount_))
    {
        RequestApplicationClose();
        return;
    }
    workspace_.Draw();
    if (createWorkspaceSmokeTest_)
    {
        static_cast<void>(workspace_.RunCreateWorkspaceSmokeStep(
            renderedFrameCount_));
    }
    if (voxelRayPickingSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelRayPickingSmokeStep(
            renderedFrameCount_, voxelRayPickingSmokeSourcePath_));
    }
    if (voxelPencilSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelPencilSmokeStep(
            renderedFrameCount_, voxelPencilSmokeSourcePath_));
    }
    if (voxelEraserSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelEraserSmokeStep(
            renderedFrameCount_, voxelEraserSmokeSourcePath_));
    }
    if (voxelUndoRedoSmokeTest_)
    {
        static_cast<void>(workspace_.RunVoxelUndoRedoSmokeStep(
            renderedFrameCount_, voxelUndoRedoSmokeSourcePath_));
    }
    if (layoutStabilitySmokeTest_)
    {
        static_cast<void>(workspace_.RunLayoutStabilitySmokeStep(
            renderedFrameCount_));
    }
    if (doubleClickCameraSmokeTest_)
    {
        static_cast<void>(workspace_.RunDoubleClickCameraSmokeStep(
            renderedFrameCount_));
    }
    if (paletteUiSmokeTest_)
    {
        static_cast<void>(workspace_.RunPaletteUiSmokeStep(
            renderedFrameCount_));
    }

    ++renderedFrameCount_;

    const bool smokeTestComplete =
        smokeTestFrameLimit_ > 0 &&
        renderedFrameCount_ >= smokeTestFrameLimit_;

    const bool saveSmokeCompletedWithCleanup =
        (voxelSaveSmokeTest_ && workspace_.VoxelSaveSmokePassed()) ||
        (firstCreationExperienceSmokeTest_ &&
            workspace_.FirstCreationExperienceSmokePassed()) ||
        (layoutStabilitySmokeTest_ &&
            workspace_.LayoutStabilitySmokePassed()) ||
        (createWorkspaceSmokeTest_ &&
            workspace_.CreateWorkspaceSmokePassed()) ||
        (doubleClickCameraSmokeTest_ &&
            workspace_.DoubleClickCameraSmokePassed()) ||
        (persistentWorkplaneSmokeTest_ &&
            workspace_.PersistentWorkplaneSmokePassed()) ||
        (projectSessionRestoreSmokeTest_ &&
            workspace_.ProjectSessionRestoreSmokePassed()) ||
        (directCreationFlowSmokeTest_ &&
            workspace_.DirectCreationFlowSmokePassed()) ||
        (paletteUiSmokeTest_ && workspace_.PaletteUiSmokePassed()) ||
        (voxelFillSmokeTest_ && workspace_.VoxelFillSmokePassed()) ||
        (voxelBoxSmokeTest_ && workspace_.VoxelBoxSmokePassed()) ||
        (voxelLineSmokeTest_ && workspace_.VoxelLineSmokePassed()) ||
        (voxelSphereSmokeTest_ && workspace_.VoxelSphereSmokePassed()) ||
        (modernToolbarSmokeTest_ && workspace_.ModernToolbarSmokePassed()) ||
        (keyboardShortcutsSmokeTest_ &&
         workspace_.KeyboardShortcutsSmokePassed()) ||
        (voxelMoveSmokeTest_ && workspace_.VoxelMoveSmokePassed()) ||
        (voxelDuplicateSmokeTest_ && workspace_.VoxelDuplicateSmokePassed()) ||
        (voxelRotateSmokeTest_ && workspace_.VoxelRotateSmokePassed()) ||
        (voxelMirrorSmokeTest_ && workspace_.VoxelMirrorSmokePassed()) ||
        (voxelScaleSmokeTest_ && workspace_.VoxelScaleSmokePassed()) ||
        (voxelAlignSmokeTest_ && workspace_.VoxelAlignSmokePassed()) ||
        (moveGizmoSmokeTest_ && workspace_.MoveGizmoSmokePassed()) ||
        (rotateGizmoSmokeTest_ && workspace_.RotateGizmoSmokePassed()) ||
        (scaleGizmoSmokeTest_ && workspace_.ScaleGizmoSmokePassed()) ||
        (transformGizmoManagerSmokeTest_ &&
            workspace_.TransformGizmoManagerSmokePassed()) ||
        (transformGizmoFoundationSmokeTest_ &&
            workspace_.TransformGizmoFoundationSmokePassed()) ||
        (transformPanelSmokeTest_ && workspace_.TransformPanelSmokePassed()) ||
        (saveOnExitSmokeTest_ && workspace_.SaveOnExitSmokePassed());
    if (requireVoxelViewportRender_ &&
        (workspace_.HasVoxelViewportRenderError() ||
         (smokeTestComplete && !workspace_.HasRenderedVoxelViewport() &&
          !saveSmokeCompletedWithCleanup)))
    {
        throw std::runtime_error("Viewport smoke test did not render a GPU frame.");
    }
    if (voxelSelectionSmokeTest_ && smokeTestComplete &&
        (!voxelSelectionRayHit_ ||
         !workspace_.SelectionSystemSmokePassed() ||
         workspace_.VoxelHighlightUploadCount() == 0U ||
         workspace_.VoxelHighlightRenderCount() < 5U))
    {
        throw std::runtime_error(
            "Voxel selection smoke test did not raycast, upload, and render highlights.");
    }
    if (eraseVoxelSmokeTest_ && smokeTestComplete &&
        !workspace_.EraseVoxelSmokePassed())
    {
        throw std::runtime_error(
            "Erase voxel smoke test did not execute, undo, redo, and render.");
    }
    if (paintVoxelSmokeTest_ && smokeTestComplete &&
        !workspace_.PaintVoxelSmokePassed())
    {
        throw std::runtime_error(
            "Paint voxel smoke test did not execute, undo, redo, and render.");
    }
    if (voxelSaveSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelSaveSmokePassed())
    {
        throw std::runtime_error(
            "Voxel save smoke test did not save, reload, and render edits.");
    }
    if (addVoxelSmokeTest_ && smokeTestComplete &&
        !workspace_.AddVoxelSmokePassed())
    {
        throw std::runtime_error(
            "Add voxel smoke test did not add, undo, redo, save, reload, and render.");
    }
    if (modelImportSmokeTest_ && smokeTestComplete &&
        !workspace_.ModelImportSmokePassed())
    {
        throw std::runtime_error(
            "Model import smoke test did not copy, refresh, open, and render.");
    }
    if (qualityOfLifeSmokeTest_ && smokeTestComplete &&
        !workspace_.QualityOfLifeSmokePassed())
    {
        throw std::runtime_error(
            "Quality of Life smoke test did not complete its controlled flow.");
    }
    if (dragDropImportSmokeTest_ && smokeTestComplete &&
        !workspace_.DragDropImportSmokePassed())
    {
        throw std::runtime_error(
            "Drag-drop import smoke test did not complete its controlled flow.");
    }
    if (voxelDocumentSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelDocumentSmokePassed())
    {
        throw std::runtime_error(
            "Voxel document smoke test did not complete its controlled flow.");
    }
    if (voxelRenderSyncSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelRenderSyncSmokePassed())
    {
        throw std::runtime_error(
            "Voxel render synchronization smoke test did not complete.");
    }
    if (voxelRayPickingSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelRayPickingSmokePassed())
    {
        throw std::runtime_error(
            "Voxel ray picking smoke test did not complete.");
    }
    if (voxelPencilSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelPencilSmokePassed())
    {
        throw std::runtime_error(
            "Voxel Pencil smoke test did not complete.");
    }
    if (voxelEraserSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelEraserSmokePassed())
    {
        throw std::runtime_error(
            "Voxel Eraser smoke test did not complete.");
    }
    if (voxelUndoRedoSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelUndoRedoSmokePassed())
    {
        throw std::runtime_error(
            "Voxel Undo/Redo smoke test did not complete.");
    }
    if (firstCreationExperienceSmokeTest_ && smokeTestComplete &&
        !workspace_.FirstCreationExperienceSmokePassed())
    {
        throw std::runtime_error(
            "First creation experience smoke test did not complete.");
    }
    if (layoutStabilitySmokeTest_ && smokeTestComplete &&
        !workspace_.LayoutStabilitySmokePassed())
    {
        throw std::runtime_error(
            "Viewport and Inspector layout stability smoke test did not complete.");
    }
    if (createWorkspaceSmokeTest_ && smokeTestComplete &&
        !workspace_.CreateWorkspaceSmokePassed())
    {
        throw std::runtime_error(
            "Create workspace smoke test did not rebuild the official layout.");
    }
    if (doubleClickCameraSmokeTest_ && smokeTestComplete &&
        !workspace_.DoubleClickCameraSmokePassed())
    {
        throw std::runtime_error(
            "Double-click camera smoke test did not complete.");
    }
    if (persistentWorkplaneSmokeTest_ && smokeTestComplete &&
        !workspace_.PersistentWorkplaneSmokePassed())
    {
        throw std::runtime_error(
            "Persistent Workplane smoke test did not complete.");
    }
    if (projectSessionRestoreSmokeTest_ && smokeTestComplete &&
        !workspace_.ProjectSessionRestoreSmokePassed())
    {
        throw std::runtime_error(
            "Project session restore smoke test did not complete.");
    }
    if (directCreationFlowSmokeTest_ && smokeTestComplete &&
        !workspace_.DirectCreationFlowSmokePassed())
    {
        throw std::runtime_error(
            "Direct creation flow smoke test did not complete.");
    }
    if (paletteUiSmokeTest_ && smokeTestComplete &&
        !workspace_.PaletteUiSmokePassed())
    {
        throw std::runtime_error("Palette UI smoke test did not complete.");
    }
    if (voxelFillSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelFillSmokePassed())
    {
        throw std::runtime_error("Voxel Fill smoke test did not complete.");
    }
    if (voxelBoxSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelBoxSmokePassed())
    {
        throw std::runtime_error("Voxel Box smoke test did not complete.");
    }
    if (voxelLineSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelLineSmokePassed())
    {
        throw std::runtime_error("Voxel Line smoke test did not complete.");
    }
    if (voxelSphereSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelSphereSmokePassed())
    {
        throw std::runtime_error("Voxel Sphere smoke test did not complete.");
    }
    if (modernToolbarSmokeTest_ && smokeTestComplete &&
        !workspace_.ModernToolbarSmokePassed())
    {
        throw std::runtime_error("Modern Toolbar smoke test did not complete.");
    }
    if (keyboardShortcutsSmokeTest_ && smokeTestComplete &&
        !workspace_.KeyboardShortcutsSmokePassed())
    {
        throw std::runtime_error(
            "Keyboard Shortcuts smoke test did not complete.");
    }
    if (voxelMoveSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelMoveSmokePassed())
    {
        throw std::runtime_error("Voxel Move smoke test did not complete.");
    }
    if (voxelDuplicateSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelDuplicateSmokePassed())
    {
        throw std::runtime_error(
            "Voxel Duplicate smoke test did not complete.");
    }
    if (voxelRotateSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelRotateSmokePassed())
    {
        throw std::runtime_error("Voxel Rotate smoke test did not complete.");
    }
    if (voxelMirrorSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelMirrorSmokePassed())
    {
        throw std::runtime_error("Voxel Mirror smoke test did not complete.");
    }
    if (voxelScaleSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelScaleSmokePassed())
    {
        throw std::runtime_error("Voxel Scale smoke test did not complete.");
    }
    if (voxelAlignSmokeTest_ && smokeTestComplete &&
        !workspace_.VoxelAlignSmokePassed())
    {
        throw std::runtime_error("Voxel Align smoke test did not complete.");
    }
    if (moveGizmoSmokeTest_ && smokeTestComplete &&
        !workspace_.MoveGizmoSmokePassed())
    {
        throw std::runtime_error("Move Gizmo smoke test did not complete.");
    }
    if (rotateGizmoSmokeTest_ && smokeTestComplete &&
        !workspace_.RotateGizmoSmokePassed())
    {
        throw std::runtime_error("Rotate Gizmo smoke test did not complete.");
    }
    if (scaleGizmoSmokeTest_ && smokeTestComplete &&
        !workspace_.ScaleGizmoSmokePassed())
    {
        throw std::runtime_error("Scale Gizmo smoke test did not complete.");
    }
    if (transformGizmoManagerSmokeTest_ && smokeTestComplete &&
        !workspace_.TransformGizmoManagerSmokePassed())
    {
        throw std::runtime_error(
            "Transform Gizmo Manager smoke test did not complete.");
    }
    if (transformGizmoFoundationSmokeTest_ && smokeTestComplete &&
        !workspace_.TransformGizmoFoundationSmokePassed())
    {
        throw std::runtime_error(
            "Transform Gizmo Foundation smoke test did not complete.");
    }
    if (transformPanelSmokeTest_ && smokeTestComplete &&
        !workspace_.TransformPanelSmokePassed())
    {
        throw std::runtime_error("Transform Panel smoke test did not complete.");
    }
    if (saveOnExitSmokeTest_ && smokeTestComplete &&
        !workspace_.SaveOnExitSmokePassed())
    {
        throw std::runtime_error("Save-on-exit smoke test did not complete.");
    }

    if (workspace_.ConsumeExitRequest() || smokeTestComplete)
    {
        RequestApplicationClose();
    }
}

bool EditorLayer::RequestWindowClose()
{
    return workspace_.RequestApplicationExit();
}

void EditorLayer::RequestApplicationClose()
{
    if (applicationCloseRequested_)
    {
        return;
    }

    applicationCloseRequested_ = true;

    if (applicationCloseCallback_)
    {
        applicationCloseCallback_();
    }
}

void EditorLayer::BuildDefaultWorkspace(
    const ImGuiID dockspaceId,
    const ImGuiViewport& viewport)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);

    const ImGuiDockNodeFlags dockNodeFlags =
        static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
        ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::DockBuilderAddNode(
        dockspaceId,
        dockNodeFlags);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport.WorkSize);

    ImGuiID centerId = dockspaceId;
    const ImGuiID leftId = ImGui::DockBuilderSplitNode(
        centerId, ImGuiDir_Left, 0.20F, nullptr, &centerId);
    const ImGuiID rightId = ImGui::DockBuilderSplitNode(
        centerId, ImGuiDir_Right, 0.24F, nullptr, &centerId);
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
        centerId, ImGuiDir_Down, 0.28F, nullptr, &centerId);

    ImGuiID bottomLeftId = bottomId;
    const ImGuiID bottomRightId = ImGui::DockBuilderSplitNode(
        bottomLeftId, ImGuiDir_Right, 0.48F, nullptr, &bottomLeftId);

    ImGui::DockBuilderDockWindow("Hierarchy", leftId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Viewport", centerId);
    ImGui::DockBuilderDockWindow("Asset Browser", bottomLeftId);
    ImGui::DockBuilderDockWindow("Console", bottomRightId);
    ImGui::DockBuilderFinish(dockspaceId);

    showHierarchy_ = true;
    showInspector_ = true;
    showViewport_ = true;
    showAssetBrowser_ = true;
    showConsole_ = true;
}

void EditorLayer::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("Fichier"))
    {
        ImGui::MenuItem("Nouveau projet", "Ctrl+N");
        ImGui::MenuItem("Ouvrir un projet", "Ctrl+O");
        ImGui::Separator();
        ImGui::MenuItem("Enregistrer", "Ctrl+S");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edition"))
    {
        ImGui::MenuItem("Annuler", "Ctrl+Z");
        ImGui::MenuItem("Retablir", "Ctrl+Y");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Affichage"))
    {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser_);
        ImGui::MenuItem("Console", nullptr, &showConsole_);
        ImGui::Separator();

        if (ImGui::MenuItem("Reinitialiser l'espace de travail"))
        {
            resetWorkspaceRequested_ = true;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Aide"))
    {
        if (ImGui::MenuItem("A propos"))
        {
            showAboutPopup_ = true;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void EditorLayer::DrawHierarchyPanel()
{
    ImGui::Begin("Hierarchy", &showHierarchy_);

    if (scene_ == nullptr)
    {
        ImGui::TextDisabled("Aucune scene chargee.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled(
        "Scene : %s (%zu objets)",
        scene_->GetName().c_str(),
        scene_->GetEntityCount());
    ImGui::Separator();

    for (const auto& entity : scene_->GetEntities())
    {
        const bool selected = selectedEntity_ == entity.get();

        if (ImGui::Selectable(
                entity->GetName().c_str(),
                selected,
                ImGuiSelectableFlags_SpanAvailWidth))
        {
            selectedEntity_ = entity.get();
        }
    }

    ImGui::End();
}

void EditorLayer::DrawInspectorPanel()
{
    ImGui::Begin("Inspector", &showInspector_);

    if (selectedEntity_ == nullptr)
    {
        ImGui::TextUnformatted("Aucun objet selectionne");
        ImGui::TextDisabled("Selectionne un objet dans la Hierarchy.");
        ImGui::End();
        return;
    }

    char nameBuffer[128]{};
    const std::string& currentName = selectedEntity_->GetName();
    const std::size_t copyLength =
        currentName.size() < 127 ? currentName.size() : 127;
    currentName.copy(nameBuffer, copyLength);

    if (ImGui::InputText("Nom", nameBuffer, sizeof(nameBuffer)))
    {
        selectedEntity_->SetName(nameBuffer);
    }

    ImGui::TextDisabled(
        "UUID : %s",
        selectedEntity_->GetId().ToString().c_str());

    if (ImGui::CollapsingHeader(
            "Transform",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& transform = selectedEntity_->GetTransform();

        ImGui::DragFloat3(
            "Position",
            transform.Position.data(),
            0.1F);
        ImGui::DragFloat3(
            "Rotation",
            transform.Rotation.data(),
            0.5F);
        ImGui::DragFloat3(
            "Echelle",
            transform.Scale.data(),
            0.05F,
            0.01F,
            100.0F);
    }

    if (ImGui::CollapsingHeader(
            "Metadata",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& metadata = selectedEntity_->GetMetadata();
        ImGui::Text("Auteur : %s", metadata.Author.c_str());
        ImGui::Text("Categorie : %s", metadata.Category.c_str());

        for (const std::string& tag : metadata.Tags)
        {
            ImGui::BulletText("%s", tag.c_str());
        }
    }

    if (ImGui::CollapsingHeader(
            "Forge DNA",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& dna = selectedEntity_->GetForgeDNA();
        ImGui::Text("Style : %s", dna.Style.c_str());
        ImGui::Text("Palette : %s", dna.Palette.c_str());
        ImGui::Text("Etat : %s", dna.State.c_str());
        ImGui::Text("Usage : %s", dna.Purpose.c_str());
    }

    ImGui::End();
}

void EditorLayer::DrawViewportPanel()
{
    ImGui::Begin(
        "Viewport",
        &showViewport_,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextDisabled("The active viewport is hosted by the Scene panel.");

    ImGui::End();
}

void EditorLayer::DrawAssetBrowserPanel()
{
    ImGui::Begin("Asset Browser", &showAssetBrowser_);
    ImGui::TextUnformatted("Assets/");
    ImGui::Separator();
    ImGui::Selectable("Models");
    ImGui::SameLine();
    ImGui::Selectable("Materials");
    ImGui::SameLine();
    ImGui::Selectable("Textures");
    ImGui::End();
}

void EditorLayer::DrawConsolePanel()
{
    ImGui::Begin("Console", &showConsole_);

    if (Renderer::Renderer::HasGPUDevice())
    {
        ImGui::TextColored(
            ImVec4(0.45F, 0.85F, 0.55F, 1.0F),
            "[INFO] Interface rendue avec SDL GPU.");

        ImGui::Text(
            "Backend : %s",
            Renderer::Renderer::GetBackendName().c_str());

        ImGui::Text(
            "Shaders : %s",
            Renderer::Renderer::GetShaderFormatsDescription().c_str());
    }
    else
    {
        ImGui::TextColored(
            ImVec4(0.95F, 0.45F, 0.35F, 1.0F),
            "[ERROR] Aucun peripherique SDL GPU actif.");
    }

    ImGui::End();
}

void EditorLayer::DrawStatusBar()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float statusBarHeight = 24.0F;

    ImGui::SetNextWindowPos(ImVec2(
        viewport->WorkPos.x,
        viewport->WorkPos.y +
            viewport->WorkSize.y -
            statusBarHeight));

    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x, statusBarHeight));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(8.0F, 4.0F));

    if (ImGui::Begin("##VoxelForgeStatusBar", nullptr, flags))
    {
        const ImGuiIO& io = ImGui::GetIO();
        const std::size_t entityCount =
            scene_ == nullptr ? 0 : scene_->GetEntityCount();

        ImGui::Text(
            "FPS : %.1f | Scene : %zu objets | GPU UI : %s | VF-0242",
            io.Framerate,
            entityCount,
            Renderer::Renderer::GetBackendName().c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace VoxelForge::Editor
