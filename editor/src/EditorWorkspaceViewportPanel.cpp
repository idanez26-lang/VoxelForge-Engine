// Viewport panel of EditorWorkspace (VF-0260 lot 7d).
// Pure code motion from EditorWorkspace.cpp.
#include "EditorWorkspace.h"
#include "EditorWorkspaceUiHelpers.h"
#include "Layout/EditorPanelNames.h"
#include "VoxelModelTransform.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelSelection/VoxelRayTransform.h"
#include "EditorWindowTitle.h"
#include "Layout/EditorDockLayout.h"
#include "Layout/PalettePanelLayout.h"
#include "ProjectSession/ProjectSessionMapping.h"
#include "Dialogs/EditorDialogStyle.h"
#include "Toolbar/EditorToolbar.h"
#include "Tools/ToolPanel.h"
#include "TransformGizmo/GizmoStyle.h"

#include "VoxelForge/Project/Project.h"
#include "VoxelForge/Project/ProjectManager.h"
#include "VoxelForge/Renderer/Renderer.h"
#include "VoxelForge/Asset/Vox/VoxImporter.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxModelConverter.h"
#include "VoxelForge/Voxel/VoxelModelSerializer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <memory>
#include <limits>
#include <string>
#include <string_view>
#include <sstream>
#include <span>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
// VF-0260 (nettoyage) : nom de panneau depuis la source partagee ;
// les assistants ImGui vivent dans EditorWorkspaceUiHelpers.h.
constexpr const char* ViewportPanelWindowName = PanelNames::Viewport;

const char* StampMirrorLabel(
    const Stamps::StampPlacementMirrorMode mirror) noexcept
{
    switch (mirror)
    {
    case Stamps::StampPlacementMirrorMode::X: return "X";
    case Stamps::StampPlacementMirrorMode::Z: return "Z";
    case Stamps::StampPlacementMirrorMode::XZ: return "XZ";
    case Stamps::StampPlacementMirrorMode::None:
    default: return "None";
    }
}
}

void EditorWorkspace::DrawScenePanel()
{
    const EditorFrameProbeScope scenePanelProbe(
        frameProbe_, EditorFrameProbeSlot::ScenePanel);
    // Lot 7a: section timestamps mirroring the UpdateVoxelHighlights ones.
    auto spSectionStart = std::chrono::steady_clock::now();
    const auto spMarkSection =
        [this, &spSectionStart](const EditorFrameProbeSlot slot)
    {
        const auto now = std::chrono::steady_clock::now();
        frameProbe_.Add(slot, std::chrono::duration<double, std::milli>(
            now - spSectionStart).count());
        spSectionStart = now;
    };
    const bool focusRequested = std::exchange(
        viewportFocusRequested_, false);
    if (focusRequested) ImGui::SetNextWindowFocus();
    const bool visible = ImGui::Begin(ViewportPanelWindowName, &showScene_);
    if (focusRequested)
    {
        viewportFocusApplied_ = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
    }
    if (!visible)
    {
        ImGui::End();
        return;
    }

    const ImVec2 scenePosition = ImGui::GetWindowPos();
    const ImVec2 sceneSize = ImGui::GetWindowSize();
    viewportDropRect_ = {
        scenePosition.x, scenePosition.y,
        scenePosition.x + sceneSize.x, scenePosition.y + sceneSize.y};

    if (!projectManager_.HasActiveProject())
    {
        DrawWelcomeScreen();
        DrawFileDropOverlay(
            viewportDropRect_, DragDropImportTarget::Viewport);
        ImGui::End();
        return;
    }

    const bool hasModel = viewportState_.HasModel();
    const VoxelViewportStatistics& statistics = viewportState_.Statistics();
    const std::string modelLabel = hasModel
        ? viewportState_.Name() + (voxelSaveState_.IsDirty() ? " *" : "")
        : "No voxel model loaded.";
    ImGui::TextUnformatted(modelLabel.c_str());
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const bool narrowToolbar = toolbarWidth < 520.0F;
    if (ImGui::Button("Frame Model"))
    {
        FrameVoxelViewport();
    }
    DrawTooltip("Frame the active voxel model");
    ImGui::SameLine();
    const char* viewNames[] = {
        "Perspective", "Front", "Back", "Left", "Right", "Top", "Bottom"};
    int selectedView = static_cast<int>(viewportCamera_.GetView());
    ImGui::SetNextItemWidth(narrowToolbar ? 90.0F : 110.0F);
    if (ImGui::Combo("##ViewportView", &selectedView, viewNames, 7))
    {
        viewportCamera_.SetView(static_cast<EditorCameraView>(selectedView));
    }
    if (!narrowToolbar) ImGui::SameLine();
    bool showGrid = viewportState_.IsGridVisible();
    if (ImGui::Checkbox("Grid", &showGrid))
        viewportState_.SetGridVisible(showGrid);
    DrawTooltip("Show or hide the ground grid");
    ImGui::SameLine();
    bool showAxes = viewportState_.AreAxesVisible();
    if (ImGui::Checkbox("Axes", &showAxes))
        viewportState_.SetAxesVisible(showAxes);
    DrawTooltip("Show or hide the world axes");
    if (toolbarWidth >= 300.0F) ImGui::SameLine();
    const char* backgroundNames[] = {"Dark", "Neutral", "Light"};
    int selectedBackground = static_cast<int>(viewportState_.Background());
    ImGui::SetNextItemWidth(90.0F);
    if (ImGui::Combo(
            "##ViewportBackground", &selectedBackground,
            backgroundNames, 3))
    {
        viewportState_.SetBackground(
            static_cast<ViewportBackground>(selectedBackground));
    }
    DrawTooltip("Choose the viewport background");

    if (voxelToolState_.IsRotateActive())
    {
        if (ImGui::Button("Left 90"))
            ExecuteInputCommand(EditorInputCommand::RotateLeft);
        DrawTooltip("Rotate the preview left by 90 degrees (Q)");
        ImGui::SameLine();
        if (ImGui::Button("Right 90"))
            ExecuteInputCommand(EditorInputCommand::RotateRight);
        DrawTooltip("Rotate the preview right by 90 degrees (Shift+Q)");
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current 90-degree rotation (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Rotate without changing the document (Esc)");
        ImGui::SameLine();
    }
    if (voxelToolState_.IsScaleActive())
    {
        if (ImGui::Button("X x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleX);
        DrawTooltip("Preview Scale X by 2 (X)");
        ImGui::SameLine();
        if (ImGui::Button("Y x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleY);
        DrawTooltip("Preview Scale Y by 2 (Y)");
        ImGui::SameLine();
        if (ImGui::Button("Z x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleZ);
        DrawTooltip("Preview Scale Z by 2 (Z)");
        ImGui::SameLine();
        if (ImGui::Button("Uniform x2"))
            ExecuteInputCommand(EditorInputCommand::ScaleUniform);
        DrawTooltip("Preview uniform Scale by 2 (U)");
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current Scale (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Scale without changing the document (Esc)");
        ImGui::SameLine();
    }
    if (voxelToolState_.IsMirrorActive())
    {
        if (ImGui::Button("Mirror X"))
            ExecuteInputCommand(EditorInputCommand::MirrorX);
        DrawTooltip("Preview Mirror X (X)");
        ImGui::SameLine();
        if (ImGui::Button("Mirror Z"))
            ExecuteInputCommand(EditorInputCommand::MirrorZ);
        DrawTooltip("Preview Mirror Z (Z)");
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current mirror (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Mirror without changing the document (Esc)");
        ImGui::SameLine();
    }
    if (voxelToolState_.IsAlignActive())
    {
        const auto directionButton = [this](
            const char* label, const VoxelAlignDirection direction)
        {
            if (ImGui::Button(label))
                static_cast<void>(BeginVoxelAlignPreview(direction));
            ImGui::SameLine();
        };
        directionButton("Left", VoxelAlignDirection::Left);
        directionButton("Right", VoxelAlignDirection::Right);
        directionButton("Bottom", VoxelAlignDirection::Bottom);
        directionButton("Top", VoxelAlignDirection::Top);
        directionButton("Front", VoxelAlignDirection::Front);
        directionButton("Back", VoxelAlignDirection::Back);
        ImGui::BeginDisabled(
            !transformPreviewModel_.IsActive() ||
            transformPreviewModel_.HasCollisions() ||
            transformPreviewModel_.HasOutOfBounds());
        if (ImGui::Button("Apply"))
            ExecuteInputCommand(EditorInputCommand::TransformApply);
        ImGui::EndDisabled();
        DrawTooltip("Apply the current alignment (Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ExecuteInputCommand(EditorInputCommand::InteractionCancel);
        DrawTooltip("Cancel Align without changing the document (Esc)");
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(
        !voxelEditHistory_.CanUndo() || voxelEditInProgress_);
    if (ImGui::Button("Undo"))
        ExecuteInputCommand(EditorInputCommand::EditUndo);
    ImGui::EndDisabled();
    const std::string undoTooltip = "Undo the last edit (" +
        std::string(editorInputService_.ShortcutLabel(
            EditorInputCommand::EditUndo)) + ")";
    DrawTooltip(undoTooltip.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !voxelEditHistory_.CanRedo() || voxelEditInProgress_);
    if (ImGui::Button("Redo"))
        ExecuteInputCommand(EditorInputCommand::EditRedo);
    ImGui::EndDisabled();
    const std::string redoTooltip = "Redo the last undone edit (" +
        std::string(editorInputService_.ShortcutLabel(
            EditorInputCommand::EditRedo)) + ")";
    DrawTooltip(redoTooltip.c_str());

    if (stampPlacementSession_.IsActive())
    {
        const bool gizmoBusy = transformGizmoManager_.IsDragging();
        ImGui::SeparatorText("STAMP PLACEMENT");
        ImGui::BeginDisabled(gizmoBusy);
        if (ImGui::RadioButton(
                "Move", stampGizmoTool_ == ActiveVoxelTool::Move))
        {
            stampGizmoTool_ = ActiveVoxelTool::Move;
            static_cast<void>(transformGizmoManager_.OnToolChanged(
                stampGizmoTool_));
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Rotate Y", stampGizmoTool_ == ActiveVoxelTool::Rotate))
        {
            stampGizmoTool_ = ActiveVoxelTool::Rotate;
            static_cast<void>(transformGizmoManager_.OnToolChanged(
                stampGizmoTool_));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool canPlaceStamp = CanPlaceLatestStampPreview();
        ImGui::BeginDisabled(!canPlaceStamp);
        if (ImGui::Button("PLACE FULL STAMP  [Enter]"))
            RequestLatestStampPlacement();
        ImGui::EndDisabled();
        DrawTooltip("Place the complete preview as one Undo/Redo operation");
        ImGui::SameLine();
        if (ImGui::Button("Cancel [Esc]"))
            ClearLatestStampPreview();
        const Stamps::StampPlacementPlan* const currentStampPlan =
            stampPlacementSession_.CurrentPlan();
        if (currentStampPlan != nullptr)
        {
            ImGui::TextDisabled(
                "%zu source voxels | %zu cells will change | %zu overlaps | %zu out of bounds",
                currentStampPlan->Statistics.TotalVoxelCount,
                currentStampPlan->Statistics.ChangedVoxelCount,
                currentStampPlan->Statistics.OverlapCount,
                currentStampPlan->Statistics.OutOfBoundsCount);
        }
    }

    bool eraseRequested = false;
    bool addRequested = false;
    ImGui::BeginDisabled(stampPlacementSession_.IsActive());
    const AddVoxelTarget addTarget = ResolveAddVoxelTarget();
    ImGui::BeginDisabled(!addTarget);
    if (ImGui::Button("Add Adjacent"))
        addRequested = true;
    ImGui::EndDisabled();
    DrawTooltip("Add a voxel next to the selected face (A)");
    ImGui::SameLine();
    const bool canErase = hasModel && voxelSelection_.Selected().has_value();
    ImGui::BeginDisabled(!canErase);
    if (ImGui::Button("Erase Selected"))
        eraseRequested = true;
    ImGui::EndDisabled();
    DrawTooltip("Erase the selected voxel (Delete)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Delete");
    if (voxelSaveState_.IsDirty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Unsaved changes");
    }

    if (hasModel)
    {
        ImGui::TextDisabled(
            "%u x %u x %u | %zu voxels | %zu faces | %zu triangles",
            statistics.Width, statistics.Height, statistics.Depth,
            statistics.OccupiedVoxelCount, statistics.TriangleCount / 2U,
            statistics.TriangleCount);
        ImGui::TextDisabled(
            "Voxel hover and tool diagnostics are shown in Inspector.");
    }
    else
    {
        ImGui::TextDisabled(
            "Right-click a .vox file and choose Open in Viewport.");
    }
    ImGui::TextDisabled(
        "F: focus | Right: orbit | Middle: pan | Wheel: zoom | Home: reset");

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0F);
    available.y = std::max(available.y, 1.0F);
    const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
    currentViewportRectangle_ = {
        imageOrigin.x, imageOrigin.y, available.x, available.y};
    viewportCamera_.SetAspectRatio(available.x / available.y);
    viewportNavigation_.Tick(ImGui::GetIO().DeltaTime);
    UpdateTransformGizmo(available.y);
    const auto width = static_cast<std::uint32_t>(available.x);
    const auto height = static_cast<std::uint32_t>(available.y);
    spMarkSection(EditorFrameProbeSlot::SpSetup);
    // Lot 7b : purge de la coalescence — l'état des highlights différé à la
    // frame précédente est résolu ici, juste avant que le renderer le
    // consomme pour cette frame.
    if (highlightsUpdatePending_) UpdateVoxelHighlights();
    const bool viewportRendered =
        [this, width, height]
        {
            const EditorFrameProbeScope renderProbe(
                frameProbe_, EditorFrameProbeSlot::ViewportRender);
            return viewportRenderer_.Render(
                width, height, viewportCamera_,
                viewportState_.IsGridVisible(),
                viewportState_.AreAxesVisible(),
                viewportState_.BackgroundColor());
        }();
    if (viewportRendered)
    {
        voxelViewportRendered_ = true;
        ImGui::Image(
            reinterpret_cast<ImTextureID>(viewportRenderer_.Texture()),
            available,
            ImVec2(0.0F, 0.0F),
            ImVec2(1.0F, 1.0F));
        DrawUniversalPreviewCursor2D();
        DrawTransformGizmoVisibilityAnchor();
        const bool imageHovered = ImGui::IsItemHovered();
        const ImGuiIO& io = ImGui::GetIO();
        SelectionHandles selectionHandles{};
        std::optional<SelectionHandle> hoveredSelectionHandle;
        if (!stampPlacementSession_.IsActive() &&
            !useViewportInteractionV2_ &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.IsMoveActive() ||
             voxelToolState_.IsDuplicateActive() ||
             voxelToolState_.IsRotateActive() ||
             voxelToolState_.IsMirrorActive() ||
             voxelToolState_.IsScaleActive() ||
             voxelToolState_.IsAlignActive()) &&
            selectionService_.EditableBounds().Valid &&
            selectionInteraction_.Mode() != SelectionInteractionMode::Creating)
        {
            const SelectionBounds& handleBounds =
                (voxelToolState_.IsRotateActive() ||
                 voxelToolState_.IsMirrorActive() ||
                 voxelToolState_.IsScaleActive() ||
                 voxelToolState_.IsAlignActive()) &&
                    transformPreviewModel_.IsActive()
                ? transformPreviewModel_.PreviewBounds()
                :
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::ResizingFace ||
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingBox ||
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingContent ||
                selectionInteraction_.Mode() ==
                    SelectionInteractionMode::DuplicatingContent
                ? selectionInteraction_.CurrentBounds()
                : selectionService_.EditableBounds();
            selectionHandles = ProjectSelectionHandles(
                handleBounds, voxelModelCenter_, currentViewportRectangle_,
                viewportCamera_.GetViewProjection());
            if (voxelToolState_.IsSelectionActive() &&
                selectionInteraction_.Mode() == SelectionInteractionMode::Idle &&
                imageHovered)
            {
                hoveredSelectionHandle = PickSelectionHandle(
                    selectionHandles, {io.MousePos.x, io.MousePos.y});
            }
            if (hoveredSelectionHandle)
                SetSelectionHandleCursor(*hoveredSelectionHandle);
            DrawSelectionHandles(
                selectionHandles,
                hoveredSelectionHandle
                    ? std::optional<SelectionFace>(hoveredSelectionHandle->Face)
                    : std::nullopt,
                selectionInteraction_.ActiveFace());
        }
        ImGui::GetWindowDrawList()->AddRect(
            imageOrigin,
            ImVec2(imageOrigin.x + available.x, imageOrigin.y + available.y),
            IM_COL32(55, 64, 78, 255));

        if (voxelToolState_.IsBoxActive() && voxelBoxInteraction_.IsActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelVoxelBox();
        }
        if (voxelToolState_.IsLineActive() && voxelLineInteraction_.IsActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelVoxelLine();
        }
        if (voxelToolState_.IsSphereActive() &&
            voxelSphereInteraction_.IsActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            CancelVoxelSphere();
        }
        const bool sceneFocused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const VoxelCameraInteraction cameraInteraction =
            ImGui::IsMouseDown(ImGuiMouseButton_Right)
                ? VoxelCameraInteraction::Orbit
                : ImGui::IsMouseDown(ImGuiMouseButton_Middle)
                ? VoxelCameraInteraction::Pan
                : (imageHovered && io.MouseWheel != 0.0F && !io.KeyCtrl)
                ? VoxelCameraInteraction::Zoom
                : VoxelCameraInteraction::None;
        const bool cameraControl =
            cameraInteraction != VoxelCameraInteraction::None;
        const bool incompatiblePopupOpen = ImGui::IsPopupOpen(
            nullptr,
            ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const DragDropImportState dropState = dragDropImport_.State();
        const bool dragDropActive =
            dropState != DragDropImportState::Idle &&
            dropState != DragDropImportState::Completed &&
            dropState != DragDropImportState::Cancelled;
        const bool selectionPointerTracking =
            selectionInteraction_.IsActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool pencilV2Active = usePencilViewportInteractionV2_ &&
            voxelToolState_.IsPencilActive() &&
            toolContext_.Smart.IsOperational() &&
            toolContext_.Smart.Geometry() == SmartGeometry::Pencil;
        const bool interactionV2PointerTracking =
            ((useViewportInteractionV2_ && viewportInteractionV2_.OwnsPointer()) ||
             (pencilV2Active && pencilViewportInteractionV2_.OwnsPointer())) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool gizmoPointerTracking =
            transformGizmoManager_.IsDragging() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        // ImGui keeps the viewport item active after MouseDown. A Smart Tool
        // stroke owns that same pointer until MouseUp, so it must be treated
        // like selection/gizmo tracking rather than as an unrelated UI edit.
        const bool smartStrokePointerTracking = !pencilV2Active &&
            smartToolStroke_.IsActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool inputBlocked =
            (ImGui::IsAnyItemActive() && !selectionPointerTracking &&
             !interactionV2PointerTracking &&
             !gizmoPointerTracking && !smartStrokePointerTracking) ||
            io.WantTextInput || incompatiblePopupOpen;
        const bool smartBrushOptionsChanged = std::exchange(
            smartBrushPreviewRefreshRequested_, false);
        const SmartBrushSizeInputResult brushSizeInput =
            EditorInputService::ResolveSmartBrushSize({
                io.MouseWheel,
                ImGui::IsKeyDown(ImGuiKey_LeftCtrl),
                ImGui::IsKeyDown(ImGuiKey_RightCtrl),
                voxelDocumentSession_.HasActiveDocument(),
                voxelToolState_.IsPencilActive() && toolContext_.Smart.IsOperational(),
                imageHovered,
                sceneFocused,
                inputBlocked || (io.WantCaptureMouse && !imageHovered),
                incompatiblePopupOpen,
                dragDropActive,
                selectionPointerTracking || gizmoPointerTracking ||
                    smartStrokePointerTracking || cameraControl,
                toolContext_.Smart.Brush().Size});
        if (brushSizeInput.Changed)
        {
            toolContext_.Smart.Brush().Size = brushSizeInput.Size;
            smartBrushSizeFeedback_.Rearm(brushSizeInput.Size,
                toolContext_.Smart.Brush().Shape, toolContext_.Smart.Action(),
                static_cast<std::uint64_t>(ImGui::GetTime() * 1000.0));
        }
        bool selectionInputAvailable = imageHovered && sceneFocused &&
            !inputBlocked && !cameraControl;
        const Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        const auto previousWorkplaneHit = workplaneHit_;
        workplaneHit_.reset();
        std::optional<VoxelRay> viewportRay;
        std::optional<VoxelRaycastHit> hoveredHit;
        VoxelPickingInteractionState pickingState =
            VoxelPickingInteractionState::Unavailable;
        if (!imageHovered)
            pickingState = VoxelPickingInteractionState::OutsideViewport;
        else if (inputBlocked || dragDropActive || !sceneFocused)
            pickingState = VoxelPickingInteractionState::Blocked;
        else if (cameraControl)
            pickingState = VoxelPickingInteractionState::CameraInteraction;
        else if (document == nullptr)
            pickingState = VoxelPickingInteractionState::NoDocument;
        else if (hasModel)
        {
            const ViewportRayBuildResult ray = BuildViewportRay(
                {io.MousePos.x, io.MousePos.y},
                currentViewportRectangle_,
                viewportCamera_.GetViewProjection(),
                viewportCamera_.GetPosition());
            if (ray.Succeeded())
            {
                viewportRay = ray.Ray;
                VoxelRaycastOptions options;
                options.Transform =
                    CenteredVoxelModelTransform(voxelModelCenter_);
                hoveredHit = RaycastVoxelDocument(
                    *document, *viewportRay, options);
                if (!hoveredHit &&
                    (voxelToolState_.IsPencilActive() ||
                     voxelToolState_.IsBoxActive() ||
                     voxelToolState_.IsLineActive() ||
                     voxelToolState_.IsSphereActive() ||
                     voxelToolState_.IsSelectionActive()))
                {
                    workplaneHit_ = workplaneService_.Intersect(
                        *document, 0U, *viewportRay, voxelModelCenter_);
                }
                pickingState = hoveredHit
                    ? VoxelPickingInteractionState::Hit
                    : VoxelPickingInteractionState::NoHit;
            }
        }
        if (layoutStabilitySmokePickingOverride_)
        {
            pickingState = *layoutStabilitySmokePickingOverride_;
            hoveredHit = layoutStabilitySmokeHitOverride_;
            workplaneHit_ = layoutStabilitySmokeWorkplaneOverride_
                ? std::optional<WorkplaneHit>(WorkplaneHit{
                    WorkplaneHitStatus::Valid,
                    layoutStabilitySmokeWorkplaneOverride_,
                    0.0F})
                : std::nullopt;
        }
        if (voxelSelection_.SetHovered(pickingState, std::move(hoveredHit)) ||
            previousWorkplaneHit != workplaneHit_ || brushSizeInput.Changed ||
            smartBrushOptionsChanged)
        {
            UpdateVoxelHighlights();
        }
        TransformGizmoPointerInput gizmoPointerInput;
        gizmoPointerInput.ScreenPosition = {io.MousePos.x, io.MousePos.y};
        gizmoPointerInput.Viewport = currentViewportRectangle_;
        gizmoPointerInput.ViewProjection = viewportCamera_.GetViewProjection();
        gizmoPointerInput.Ray = viewportRay;
        const std::optional<SelectionBounds> stampBounds =
            CurrentStampPreviewBounds();
        const bool stampGizmo = stampPlacementSession_.IsActive() &&
            stampBounds.has_value();
        const bool selectionGizmoToolAvailable = !useViewportInteractionV2_ &&
            ((voxelToolState_.IsMoveActive() && CanMoveSelection()) ||
             (voxelToolState_.IsRotateActive() && CanRotateSelection()) ||
             (voxelToolState_.IsScaleActive() && CanScaleSelection()));
        const bool gizmoToolAvailable = stampGizmo ||
            selectionGizmoToolAvailable;
        const SelectionBounds gizmoContextBounds = stampGizmo
            ? stampGizmoDragActive_
                ? stampGizmoDragStartBounds_ : *stampBounds
            : selectionService_.EditableBounds();
        const TransformGizmoRuntimeContext gizmoContext{
            document != nullptr,
            document != nullptr && voxelDocumentSession_.Generation() != 0U,
            stampGizmo ? gizmoContextBounds.Valid
                       : !selectionService_.Empty() &&
                            selectionService_.EditableBounds().Valid,
            gizmoToolAvailable,
            sceneFocused && currentViewportRectangle_.Width > 0.0F &&
                currentViewportRectangle_.Height > 0.0F,
            imageHovered,
            inputBlocked,
            cameraControl,
            dragDropActive,
            closeRequest_.State() != EditorCloseRequestState::None,
            stampGizmo ? stampPlacementSession_.CurrentPreview() != nullptr
                       : document == nullptr ||
                            !transformPreviewModel_.IsActive() ||
                            transformPreviewModel_.IsValidFor(
                                *document, selectionService_,
                                voxelDocumentSession_.Generation()),
            stampGizmo ? stampGizmoTool_ : voxelToolState_.ActiveTool(),
            voxelDocumentSession_.Generation(),
            gizmoContextBounds};
        const TransformGizmoCancellation gizmoContextCancellation =
            transformGizmoManager_.UpdateContext(gizmoContext);
        if (gizmoContextCancellation)
            CancelTransformGizmoInteraction();
        const bool gizmoInputAvailable =
            transformGizmoManager_.CanBeginInteraction();
        if (!transformGizmoManager_.IsDragging())
            static_cast<void>(
                transformGizmoManager_.UpdateHover(gizmoPointerInput));
        const bool gizmoAxisHovered =
            transformGizmoManager_.HoveredAxis() !=
            TransformGizmoAxis::None;
        if (transformGizmoManager_.CursorRecommendation() ==
            TransformGizmoCursorRecommendation::ResizeAll)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        bool gizmoCaptured = false;
        if (gizmoInputAvailable && gizmoAxisHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            gizmoCaptured =
                transformGizmoManager_.BeginInteraction(gizmoPointerInput);
            if (gizmoCaptured && stampGizmo)
            {
                stampGizmoDragActive_ = true;
                stampGizmoDragStartTarget_ =
                    stampPlacementSession_.Target();
                stampGizmoDragStartQuarterTurns_ =
                    stampPlacementSession_.QuarterRotation();
                stampGizmoDragStartAxis_ =
                    stampPlacementSession_.RotationAxis();
                stampGizmoDragStartBounds_ = *stampBounds;
            }
            else if (gizmoCaptured &&
                (voxelToolState_.IsMoveActive() ||
                 voxelToolState_.IsScaleActive()))
                gizmoCaptured = transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation());
            if (!gizmoCaptured)
                CancelTransformGizmoInteraction();
            else
            {
                voxelMoveStatusMessage_.clear();
                voxelSelectionClickCandidate_ = false;
                selectionPointerAnchor_.reset();
                UpdateVoxelHighlights();
            }
        }
        if (transformGizmoManager_.IsDragging() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) && document &&
            transformGizmoManager_.UpdateInteraction(gizmoPointerInput))
        {
            if (stampGizmoDragActive_)
            {
                // STAMP-24 : l'anneau saisi decide de l'axe. Tant qu'aucun
                // anneau n'est saisi (mode Move), on conserve l'axe de depart.
                const bool rotating =
                    transformGizmoManager_.Mode() == TransformGizmoMode::Rotate;
                Stamps::StampPlacementRotationAxis dragAxis =
                    stampGizmoDragStartAxis_;
                if (rotating)
                {
                    switch (transformGizmoManager_.ActiveAxis())
                    {
                    case TransformGizmoAxis::X:
                        dragAxis =
                            Stamps::StampPlacementRotationAxis::LateralX;
                        break;
                    case TransformGizmoAxis::Z:
                        dragAxis =
                            Stamps::StampPlacementRotationAxis::DepthZ;
                        break;
                    default:
                        dragAxis =
                            Stamps::StampPlacementRotationAxis::VerticalY;
                        break;
                    }
                    stampRotationAxis_ = dragAxis;
                }
                const std::uint8_t baseTurns =
                    rotating && dragAxis != stampGizmoDragStartAxis_
                        ? 0U : stampGizmoDragStartQuarterTurns_;
                const bool moved = stampPreview_.ApplyGizmoDelta(
                    stampGizmoDragStartTarget_,
                    baseTurns,
                    transformGizmoManager_.Mode() == TransformGizmoMode::Move
                        ? transformGizmoManager_.Delta()
                        : Asset::Voxel::VoxelPosition{},
                    rotating ? transformGizmoManager_.QuarterTurns() : 0,
                    dragAxis);
                if (!moved) CancelTransformGizmoInteraction();
            }
            else if (transformGizmoManager_.Mode() == TransformGizmoMode::Move)
                static_cast<void>(transformPreviewModel_.SetDelta(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation(),
                    ConstrainMoveDelta(transformGizmoManager_.Delta())));
            else if (transformGizmoManager_.Mode() ==
                     TransformGizmoMode::Rotate)
            {
                const TransformGizmoAxis axis =
                    transformGizmoManager_.ActiveAxis();
                const VoxelRotationAxis rotationAxis =
                    axis == TransformGizmoAxis::X ? VoxelRotationAxis::X :
                    axis == TransformGizmoAxis::Y ? VoxelRotationAxis::Y :
                    VoxelRotationAxis::Z;
                const std::int32_t turns =
                    transformGizmoManager_.QuarterTurns();
                if ((turns % 4) == 0)
                    static_cast<void>(transformPreviewModel_.CancelPreview());
                else
                    static_cast<void>(BeginVoxelRotatePreview(
                        rotationAxis, turns));
            }
            else
            {
                const TransformGizmoAxis axis =
                    transformGizmoManager_.ActiveAxis();
                const VoxelScaleMode scaleMode =
                    axis == TransformGizmoAxis::X ? VoxelScaleMode::X :
                    axis == TransformGizmoAxis::Y ? VoxelScaleMode::Y :
                    VoxelScaleMode::Z;
                static_cast<void>(UpdateVoxelScalePreview(
                    scaleMode,
                    transformGizmoManager_.TargetDimensions()));
            }
            UpdateVoxelHighlights();
        }
        if (transformGizmoManager_.IsDragging() &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            CancelTransformGizmoInteraction();
        const bool gizmoConsumesPointer = gizmoCaptured || gizmoAxisHovered ||
            transformGizmoManager_.IsDragging();
        if (stampPlacementSession_.IsActive())
            selectionInputAvailable = false;
        const bool interactionV2ToolActive =
            !stampPlacementSession_.IsActive() && useViewportInteractionV2_ &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.IsMoveActive());
        if (interactionV2ToolActive)
        {
            InteractionV2::ViewportInputFrame interactionInput;
            interactionInput.Frame =
                static_cast<std::uint64_t>(ImGui::GetFrameCount());
            interactionInput.MouseScreen = {io.MousePos.x, io.MousePos.y};
            interactionInput.PrimaryPressed =
                ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            interactionInput.PrimaryHeld =
                ImGui::IsMouseDown(ImGuiMouseButton_Left);
            interactionInput.PrimaryReleased =
                ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            interactionInput.EscapePressed =
                ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            interactionInput.Control = io.KeyCtrl;
            interactionInput.Shift = io.KeyShift;
            interactionInput.ViewportHovered = imageHovered ||
                viewportInteractionV2_.OwnsPointer();
            interactionInput.ViewportFocused = sceneFocused;
            interactionInput.UiCapturesPointer = inputBlocked &&
                !viewportInteractionV2_.OwnsPointer();
            interactionInput.CameraActive = cameraControl;
            interactionInput.SelectionToolActive =
                voxelToolState_.IsSelectionActive();
            interactionInput.MoveToolActive =
                voxelToolState_.IsMoveActive();
            interactionInput.Viewport = currentViewportRectangle_;
            interactionInput.ViewProjection =
                viewportCamera_.GetViewProjection();
            interactionInput.CameraWorldPosition =
                viewportCamera_.GetPosition();
            interactionInput.ModelCenter = voxelModelCenter_;
            interactionInput.FramebufferScale =
                std::max(1.0F, io.DisplayFramebufferScale.x);
            interactionInput.DocumentGeneration =
                voxelDocumentSession_.Generation();
            interactionInput.DocumentRevision =
                document != nullptr ? document->GetRevision() : 0U;
            interactionInput.PointerRay = viewportRay;
            {
                const EditorFrameProbeScope interactionProbe(
                    frameProbe_, EditorFrameProbeSlot::InteractionTick);
                viewportInteractionV2_.SubmitInput(
                    std::move(interactionInput));
                viewportInteractionV2_.Tick(
                    document,
                    selectionService_);
            }
            CommitViewportInteractionV2Move();

            const InteractionV2::ViewportPresentation& presentation =
                viewportInteractionV2_.Presentation();
            const std::optional<SelectionBounds> renderedBounds =
                presentation.Phase == InteractionV2::InteractionPhase::Selecting
                ? std::nullopt : presentation.SelectionBox;
            viewportRenderer_.ConfigureInteractionV2(
                presentation.SelectionDetail, renderedBounds,
                presentation.MovePreview, voxelModelCenter_,
                presentation.Revision, true);
            viewportInteractionV2_.SetPresentationGpuMetrics(
                viewportRenderer_.InteractionV2UploadCount(),
                viewportRenderer_.InteractionV2BufferRecreationCount(),
                viewportRenderer_.InteractionV2UploadedBytes(),
                viewportRenderer_.InteractionV2MoveSourceUploadCount(),
                viewportRenderer_.InteractionV2MoveSourceUploadedBytes(),
                viewportRenderer_.InteractionV2MoveDeltaUpdateCount());
            DrawViewportInteractionV2Overlay();
        }
        else if (useViewportInteractionV2_)
        {
            viewportInteractionV2_.Reset();
            viewportRenderer_.ConfigureInteractionV2(
                {}, std::nullopt, nullptr, voxelModelCenter_, 0U, false);
        }
        if (pencilV2Active)
        {
            InteractionV2::PencilViewportInputFrame pencilInput;
            pencilInput.Frame = static_cast<std::uint64_t>(ImGui::GetFrameCount());
            pencilInput.PrimaryPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            pencilInput.PrimaryHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            pencilInput.PrimaryReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            pencilInput.EscapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            pencilInput.PencilToolActive = true;
            pencilInput.Interaction = {imageHovered ||
                    pencilViewportInteractionV2_.OwnsPointer(),
                sceneFocused, inputBlocked && !pencilViewportInteractionV2_.OwnsPointer(),
                cameraControl, !sceneFocused};
            if (const std::optional<PencilCompactRequest> request =
                    BuildPencilCompactRequest())
            {
                pencilInput.Request = *request;
                pencilInput.Target = request->Placement.Target;
            }
            {
                const EditorFrameProbeScope pencilTickProbe(
                    frameProbe_, EditorFrameProbeSlot::PencilTick);
                pencilViewportInteractionV2_.SubmitInput(
                    std::move(pencilInput));
                pencilViewportInteractionV2_.Tick(document);
            }
            {
                const EditorFrameProbeScope pencilCommitProbe(
                    frameProbe_, EditorFrameProbeSlot::PencilCommit);
                CommitPencilViewportInteractionV2();
            }
            const InteractionV2::PencilCompactPresentation& presentation =
                pencilViewportInteractionV2_.Presentation();
            if (pencilV2RenderedPresentationRevision_ != presentation.Revision)
                UpdateVoxelHighlights();
            // Pencil V2 deliberately reuses the existing highlight renderer.
            // These are observed renderer counters only: the controller never
            // asks the renderer to calculate a footprint or materialize a
            // compact preview.  There is currently no dedicated Pencil V2
            // GPU buffer, so unavailable source/delta counters stay zero.
            pencilViewportInteractionV2_.SetRendererMetrics(
                viewportRenderer_.HighlightUploadCount(), 0U,
                viewportRenderer_.HighlightRenderCount(), 0U, 0U);
        }
        else if (usePencilViewportInteractionV2_)
        {
            pencilViewportInteractionV2_.Reset();
            pencilV2PreviewPositions_.clear();
            pencilV2RenderedPresentationRevision_ = 0U;
        }
        const bool doubleClickFocus = !useViewportInteractionV2_ &&
            !pencilV2Active &&
            !gizmoConsumesPointer &&
            selectionInputAvailable &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            voxelSelection_.Hovered().has_value();
        if (doubleClickFocus)
        {
            const auto& hit = *voxelSelection_.Hovered();
            const Asset::Voxel::VoxelPosition position{
                static_cast<std::int32_t>(hit.Coordinates.X),
                static_cast<std::int32_t>(hit.Coordinates.Y),
                static_cast<std::int32_t>(hit.Coordinates.Z)};
            static_cast<void>(selectionService_.Select(position));
            UpdateVoxelHighlights();
            static_cast<void>(viewportNavigation_.FocusSelection(
                SelectionNavigationBounds()));
        }
        std::optional<SelectionBoxRayHit> hoveredSelectionInterior;
        if (!useViewportInteractionV2_ &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.IsMoveActive() ||
             voxelToolState_.IsDuplicateActive()) &&
            selectionInteraction_.Mode() == SelectionInteractionMode::Idle &&
            selectionService_.EditableBounds().Valid &&
            selectionInputAvailable && viewportRay && !gizmoConsumesPointer &&
            !hoveredSelectionHandle)
        {
            std::optional<float> occluderDistance;
            if (const auto& voxelHit = voxelSelection_.Hovered())
            {
                const Asset::Voxel::VoxelPosition hitPosition{
                    static_cast<std::int32_t>(voxelHit->Coordinates.X),
                    static_cast<std::int32_t>(voxelHit->Coordinates.Y),
                    static_cast<std::int32_t>(voxelHit->Coordinates.Z)};
                if (!selectionService_.EditableBounds().Contains(hitPosition))
                    occluderDistance = voxelHit->Distance;
            }
            hoveredSelectionInterior = PickSelectionBoxInterior(
                selectionService_.EditableBounds(), voxelModelCenter_,
                *viewportRay, occluderDistance);
        }
        const bool interiorHovered = hoveredSelectionInterior.has_value();
        if (selectionBoxInteriorHovered_ != interiorHovered)
        {
            selectionBoxInteriorHovered_ = interiorHovered;
            UpdateVoxelHighlights();
        }
        if ((interiorHovered || selectionInteraction_.Mode() ==
                SelectionInteractionMode::MovingBox ||
             selectionInteraction_.Mode() ==
                SelectionInteractionMode::MovingContent ||
             selectionInteraction_.Mode() ==
                SelectionInteractionMode::DuplicatingContent) &&
            !hoveredSelectionHandle)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        std::string stampViewportHelp;
        const char* viewportHelp = "Click a voxel to select it";
        if (stampPlacementSession_.IsActive())
        {
            stampViewportHelp =
                "Stamp placement - Drag gizmo axes to move - Select Rotation "
                "gizmo then drag the X, Y or Z ring - Enter places the full "
                "Stamp - Rotation " +
                std::string(Stamps::StampRotationAxisLabel(
                    stampPlacementSession_.RotationAxis())) + ": " +
                std::to_string(stampPlacementSession_.RotationDegrees()) +
                " deg" +
                (stampPlacementSession_.HalfQuarterStep()
                        ? " (approximate)"
                        : "") +
                " - Step: " +
                (stampPlacementSession_.RotationStep() ==
                            Stamps::StampRotationStep::Eighth45
                        ? "45"
                        : "90") +
                " deg - Mirror: " +
                StampMirrorLabel(stampPlacementSession_.Mirror()) +
                " - Q/Shift+Q rotate - Shift+E step 90/45 - X/Z toggle - "
                "M cycle - Shift+M reset - Esc cancel";
            viewportHelp = stampViewportHelp.c_str();
        }
        else if (voxelToolState_.IsSelectionActive())
        {
            if (selectionInteraction_.Mode() ==
                SelectionInteractionMode::Creating)
                viewportHelp = "Drag to size selection box — Release to validate";
            else if (selectionInteraction_.Mode() ==
                SelectionInteractionMode::ResizingFace)
                viewportHelp = "Resize selection box — Release to validate — Esc to cancel";
            else if (selectionInteraction_.Mode() ==
                SelectionInteractionMode::MovingBox)
                viewportHelp = "Move selection box — Release to validate — Esc to cancel";
            else if (hoveredSelectionHandle)
                viewportHelp = "Drag to resize this face";
            else if (interiorHovered)
                viewportHelp = "Drag to move the selection box";
            else if (selectionService_.EditableBounds().Valid)
                viewportHelp = "Drag a face handle to resize — Drag inside to move the selection box";
            else
                viewportHelp = "Click-drag to draw a selection box — Click to select one voxel";
        }
        else if (voxelToolState_.IsMoveActive())
        {
            if (transformGizmoManager_.IsDragging())
            {
                const std::string_view gizmoHelp =
                    transformGizmoManager_.HelpText();
                viewportHelp = transformPreviewModel_.HasCollisions()
                    ? "Move blocked: destination is occupied"
                    : transformPreviewModel_.HasOutOfBounds()
                    ? "Move blocked: destination is outside the model"
                    : !gizmoHelp.empty()
                    ? gizmoHelp.data()
                    : "Moving — Release to apply — Esc to cancel";
            }
            else if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingContent)
            {
                viewportHelp = transformPreviewModel_.HasCollisions()
                    ? "Move blocked: destination is occupied"
                    : transformPreviewModel_.HasOutOfBounds()
                    ? "Move blocked: destination is outside the model"
                    : "Move voxels — Release to validate — Esc to cancel";
            }
            else
            {
                const std::string_view gizmoHelp =
                    transformGizmoManager_.HelpText();
                viewportHelp = !gizmoHelp.empty()
                    ? gizmoHelp.data()
                    : !voxelMoveStatusMessage_.empty()
                    ? voxelMoveStatusMessage_.c_str()
                    : CanMoveSelection()
                    ? "Drag an axis or drag inside the selection to move its voxels"
                    : "Select voxels before using Move";
            }
        }
        else if (voxelToolState_.IsDuplicateActive())
        {
            if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::DuplicatingContent)
            {
                viewportHelp = transformPreviewModel_.HasCollisions()
                    ? "Duplicate blocked: destination is occupied"
                    : transformPreviewModel_.HasOutOfBounds()
                    ? "Duplicate blocked: destination is outside the model"
                    : "Duplicate voxels — Release to validate — Esc to cancel";
            }
            else
            {
                viewportHelp = !voxelDuplicateStatusMessage_.empty()
                    ? voxelDuplicateStatusMessage_.c_str()
                    : CanDuplicateSelection()
                    ? "Drag inside the selection to duplicate its voxels"
                    : "Select voxels before using Duplicate";
            }
        }
        else if (voxelToolState_.IsRotateActive())
        {
            const std::string_view gizmoHelp =
                transformGizmoManager_.HelpText();
            viewportHelp = transformPreviewModel_.HasCollisions()
                ? "Rotate blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Rotate blocked: destination is outside the model"
                : !gizmoHelp.empty()
                ? gizmoHelp.data()
                : !voxelRotateStatusMessage_.empty()
                ? voxelRotateStatusMessage_.c_str()
                : !transformPreviewModel_.IsActive()
                ? "Drag a rotation ring — Q left — Shift+Q right — Esc to exit"
                : voxelRotateDirection_ == VoxelRotationDirection::Clockwise
                ? "Rotate Y: +90° — Enter to apply — Esc to cancel"
                : "Rotate Y: -90° — Enter to apply — Esc to cancel";
        }
        else if (voxelToolState_.IsMirrorActive())
        {
            viewportHelp = !voxelMirrorStatusMessage_.empty()
                ? voxelMirrorStatusMessage_.c_str()
                : !transformPreviewModel_.IsActive()
                ? "Choose X or Z to preview a mirror"
                : transformPreviewModel_.HasCollisions()
                ? "Mirror blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Mirror blocked: destination is outside the model"
                : voxelMirrorAxis_ == VoxelMirrorAxis::X
                ? "Mirror X — Enter to apply — Esc to cancel"
                : "Mirror Z — Enter to apply — Esc to cancel";
        }
        else if (voxelToolState_.IsScaleActive())
        {
            const std::string_view gizmoHelp =
                transformGizmoManager_.HelpText();
            viewportHelp = !voxelScaleStatusMessage_.empty()
                ? voxelScaleStatusMessage_.c_str()
                : transformPreviewModel_.HasCollisions()
                ? "Scale blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Scale blocked: destination is outside the model"
                : !gizmoHelp.empty()
                ? gizmoHelp.data()
                : !transformPreviewModel_.IsActive()
                ? "Choose X, Y, Z or U to preview Scale x2"
                : voxelScaleMode_ == VoxelScaleMode::Uniform
                ? "Scale Uniform x2 - Enter to apply - Esc to cancel"
                : voxelScaleMode_ == VoxelScaleMode::X
                ? "Scale X x2 - Enter to apply - Esc to cancel"
                : voxelScaleMode_ == VoxelScaleMode::Y
                ? "Scale Y x2 - Enter to apply - Esc to cancel"
                : "Scale Z x2 - Enter to apply - Esc to cancel";
        }
        else if (voxelToolState_.IsAlignActive())
        {
            viewportHelp = !voxelAlignStatusMessage_.empty()
                ? voxelAlignStatusMessage_.c_str()
                : !transformPreviewModel_.IsActive()
                ? "Choose Left, Right, Bottom, Top, Front or Back"
                : transformPreviewModel_.HasCollisions()
                ? "Align blocked: destination is occupied"
                : transformPreviewModel_.HasOutOfBounds()
                ? "Align blocked: destination is outside the model"
                : "Align preview - Enter to apply - Esc to cancel";
        }
        DrawTooltip(viewportHelp);
        if (stampPlacementSession_.IsActive() ||
            voxelToolState_.IsSelectionActive() ||
            voxelToolState_.IsMoveActive() ||
            voxelToolState_.IsDuplicateActive() ||
            voxelToolState_.IsRotateActive() ||
            voxelToolState_.IsMirrorActive() ||
            voxelToolState_.IsScaleActive() ||
            voxelToolState_.IsAlignActive())
        {
            const ImVec2 textSize = ImGui::CalcTextSize(viewportHelp);
            const ImVec2 helpMinimum{imageOrigin.x + 10.0F, imageOrigin.y + 10.0F};
            const ImVec2 helpMaximum{
                helpMinimum.x + textSize.x + 16.0F,
                helpMinimum.y + textSize.y + 10.0F};
            ImGui::GetWindowDrawList()->AddRectFilled(
                helpMinimum, helpMaximum, IM_COL32(8, 13, 20, 210), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(
                helpMinimum, helpMaximum, IM_COL32(55, 220, 190, 230), 4.0F);
            ImGui::GetWindowDrawList()->AddText(
                {helpMinimum.x + 8.0F, helpMinimum.y + 5.0F},
                IM_COL32(225, 245, 242, 255), viewportHelp);
        }
        if (voxelToolState_.IsBoxActive() && voxelBoxInteraction_.IsActive() &&
            voxelBoxInteraction_.Update(CurrentTwoPointToolTarget()))
        {
            UpdateVoxelHighlights();
        }
        if (voxelToolState_.IsLineActive() && voxelLineInteraction_.IsActive() &&
            voxelLineInteraction_.Update(CurrentTwoPointToolTarget()))
        {
            UpdateVoxelHighlights();
        }
        if (voxelToolState_.IsSphereActive() &&
            voxelSphereInteraction_.IsActive() &&
            voxelSphereInteraction_.Update(CurrentTwoPointToolTarget()))
        {
            UpdateVoxelHighlights();
        }
        const VoxelToolInputDecision toolDecision =
            voxelToolInput_.Update({
                ImGui::IsMouseDown(ImGuiMouseButton_Left),
                voxelToolState_.IsEditingToolActive(),
                document != nullptr,
                imageHovered,
                sceneFocused,
                (io.WantCaptureMouse && (!imageHovered || inputBlocked)) ||
                    gizmoConsumesPointer,
                incompatiblePopupOpen,
                dragDropActive,
                cameraInteraction,
                voxelEditInProgress_,
                voxelDocumentSession_.Generation()});
        const bool smartContinuousTool = !pencilV2Active &&
            voxelToolState_.IsPencilActive() &&
            toolContext_.Smart.IsOperational() &&
            toolContext_.Smart.Geometry() != SmartGeometry::Fill &&
            (toolContext_.Smart.Action() == SmartAction::Add ||
             toolContext_.Smart.Action() == SmartAction::Paint ||
             toolContext_.Smart.Action() == SmartAction::Erase);
        const bool smartStrokeMayContinue = document != nullptr && imageHovered &&
            sceneFocused && !((io.WantCaptureMouse && (!imageHovered || inputBlocked)) ||
                gizmoConsumesPointer) && !incompatiblePopupOpen && !dragDropActive &&
            cameraInteraction == VoxelCameraInteraction::None && !voxelEditInProgress_;
        // A continuous stroke is valid only while its original interaction
        // context remains intact. Leaving the viewport, losing focus, a
        // popup, drag/drop, camera interaction, tool or action change must
        // cancel (not commit) the pending atomic edit. An invalid voxel
        // target is handled inside ContinueSmartToolStroke() and merely
        // suspends the segment so a later valid target starts a new one.
        if (smartToolStroke_.IsActive() &&
            (!smartContinuousTool || !smartStrokeMayContinue ||
             toolContext_.Smart.Action() != smartToolStroke_.Action() ||
             (smartLineLockedStart_ &&
                 toolContext_.Smart.Geometry() != SmartGeometry::Line) ||
             (smartGeometryPlane_ &&
                 (toolContext_.Smart.Geometry() != SmartGeometry::Geometry ||
                  toolContext_.Smart.Mode() != smartGeometryLockedMode_ ||
                  toolContext_.Smart.Action() != smartGeometryLockedAction_)) ||
             (smartSurfacePlane_ &&
                 toolContext_.Smart.Geometry() != SmartGeometry::Surface)))
            CancelSmartToolStroke();
        if (!doubleClickFocus && smartContinuousTool)
        {
            const bool cylinderHeightPhase = smartToolStroke_.IsActive() &&
                toolContext_.Smart.Geometry() == SmartGeometry::Geometry &&
                smartGeometryLockedMode_ == SmartToolMode::CylinderBrush &&
                smartGeometryPhase_ == SmartGeometryInteractionPhase::Height;
            if (cylinderHeightPhase &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                static_cast<void>(CommitSmartToolStroke());
                UpdateVoxelHighlights();
            }
            else if (cylinderHeightPhase)
            {
                if (smartStrokeMayContinue)
                {
                    constexpr float pixelsPerGeometryLayer = 24.0F;
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    smartGeometryHeight_ = ResolveSmartToolGeometryHeight(
                        smartGeometryHeightDragAxis_.value_or(
                            SmartToolFaceDepthDragAxis{}),
                        {mouse.x - smartGeometryHeightStartMouse_.X,
                         mouse.y - smartGeometryHeightStartMouse_.Y},
                        MaximumSmartGeometryHeight, pixelsPerGeometryLayer);
                    if (ContinueSmartToolStroke())
                        UpdateVoxelHighlights();
                }
                else CancelSmartToolStroke();
            }
            else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (smartToolStroke_.IsActive())
                {
                    if (toolContext_.Smart.Geometry() ==
                            SmartGeometry::Geometry &&
                        smartGeometryLockedMode_ ==
                            SmartToolMode::CylinderBrush &&
                        smartGeometryEndpointValid_)
                    {
                        smartGeometryPhase_ =
                            SmartGeometryInteractionPhase::Height;
                        const ImVec2 mouse = ImGui::GetIO().MousePos;
                        smartGeometryHeightStartMouse_ = {mouse.x, mouse.y};
                        smartGeometryHeight_ = 1;
                    }
                    else
                        static_cast<void>(CommitSmartToolStroke());
                    UpdateVoxelHighlights();
                }
            }
            else if (!smartToolStroke_.IsActive())
            {
                if (toolDecision == VoxelToolInputDecision::Apply &&
                    BeginSmartToolStroke())
                    UpdateVoxelHighlights();
            }
            else if (smartStrokeMayContinue)
            {
                if (smartToolStroke_.IsActive() &&
                    toolContext_.Smart.Geometry() == SmartGeometry::Face &&
                    smartToolStroke_.Action() == SmartAction::Add)
                {
                    // The axis was projected once when the face was locked.
                    // Thus X/Z faces follow their visible screen direction,
                    // while camera motion cannot perturb a live extrusion.
                    constexpr float pixelsPerFaceLayer = 24.0F;
                    const ImVec2 drag = ImGui::GetMouseDragDelta(
                        ImGuiMouseButton_Left, 0.0F);
                    faceDepthLayers_ = ResolveSmartToolFaceDepthLayers(
                        faceDepthDragAxis_.value_or(SmartToolFaceDepthDragAxis{}),
                        {drag.x, drag.y}, 1, MaximumSmartToolBrushSize,
                        pixelsPerFaceLayer);
                }
                const bool previewPlanWasAvailable =
                    smartToolStrokePreviewPlan_ != nullptr;
                const bool strokeChanged = ContinueSmartToolStroke();
                if (strokeChanged ||
                    (previewPlanWasAvailable &&
                     smartToolStrokePreviewPlan_ == nullptr))
                    UpdateVoxelHighlights();
            }
            else CancelSmartToolStroke();
        }
        else if (!pencilV2Active && !doubleClickFocus &&
                 toolDecision == VoxelToolInputDecision::Apply)
        {
            if (voxelToolState_.IsPencilActive())
            {
                if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Geometry() == SmartGeometry::Fill)
                    static_cast<void>(ApplySmartFill());
                else if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Action() == SmartAction::Add)
                    static_cast<void>(ApplyVoxelPencil());
                else if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Action() == SmartAction::Erase)
                    static_cast<void>(ApplyVoxelPencil());
                else if (toolContext_.Smart.IsOperational() &&
                    toolContext_.Smart.Action() == SmartAction::Paint)
                    static_cast<void>(ApplyVoxelPencil());
            }
            else if (voxelToolState_.IsEraserActive())
            {
                toolContext_.Smart.SetGeometry(SmartGeometry::Pencil);
                toolContext_.Smart.SetAction(SmartAction::Erase);
                SelectVoxelTool(ActiveVoxelTool::Pencil);
                static_cast<void>(ApplyVoxelPencil());
            }
            else if (voxelToolState_.IsFillActive())
                static_cast<void>(ApplyVoxelPaintBrush());
            else if (voxelToolState_.IsBoxActive())
            {
                const auto target = CurrentTwoPointToolTarget();
                if (target)
                {
                    if (!voxelBoxInteraction_.IsActive())
                    {
                        static_cast<void>(voxelBoxInteraction_.Begin(
                            *target, voxelDocumentSession_.Generation()));
                        UpdateVoxelHighlights();
                    }
                    else
                    {
                        static_cast<void>(voxelBoxInteraction_.Update(target));
                        static_cast<void>(ApplyVoxelBox());
                    }
                }
            }
            else if (voxelToolState_.IsLineActive())
            {
                const auto target = CurrentTwoPointToolTarget();
                if (target)
                {
                    if (!voxelLineInteraction_.IsActive())
                    {
                        static_cast<void>(voxelLineInteraction_.Begin(
                            *target, voxelDocumentSession_.Generation()));
                        UpdateVoxelHighlights();
                    }
                    else
                    {
                        static_cast<void>(voxelLineInteraction_.Update(target));
                        static_cast<void>(ApplyVoxelLine());
                    }
                }
            }
            else if (voxelToolState_.IsSphereActive())
            {
                const auto target = CurrentTwoPointToolTarget();
                if (target)
                {
                    if (!voxelSphereInteraction_.IsActive())
                    {
                        static_cast<void>(voxelSphereInteraction_.Begin(
                            *target, voxelDocumentSession_.Generation()));
                        UpdateVoxelHighlights();
                    }
                    else
                    {
                        static_cast<void>(voxelSphereInteraction_.Update(target));
                        static_cast<void>(ApplyVoxelSphere());
                    }
                }
            }
        }

        if (!useViewportInteractionV2_)
        {
        const SelectionPointerTarget selectionPointerTarget =
            ResolveSelectionPointerTarget(
                hoveredSelectionHandle.has_value(), interiorHovered);
        bool selectionHandleCaptured = false;
        if (voxelToolState_.IsSelectionActive() &&
            selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Handle &&
            hoveredSelectionHandle &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            selectionHandleCaptured = selectionInteraction_.BeginResizingFace(
                hoveredSelectionHandle->Face,
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(),
                io.MousePos.x, io.MousePos.y,
                hoveredSelectionHandle->ScreenAxisPerVoxel);
            if (selectionHandleCaptured)
            {
                voxelSelectionClickCandidate_ = false;
                selectionPointerAnchor_.reset();
                UpdateVoxelHighlights();
            }
        }
        bool selectionBoxCaptured = false;
        if (!selectionHandleCaptured && voxelToolState_.IsSelectionActive() &&
            selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Interior &&
            hoveredSelectionInterior && viewportRay &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const SelectionMovePlane movePlane = MakeSelectionMovePlane(
                hoveredSelectionInterior->WorldPosition,
                viewportRay->Direction);
            selectionBoxCaptured = selectionInteraction_.BeginMovingBox(
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(), movePlane,
                hoveredSelectionInterior->WorldPosition);
            if (selectionBoxCaptured)
            {
                voxelSelectionClickCandidate_ = false;
                selectionPointerAnchor_.reset();
                UpdateVoxelHighlights();
            }
        }
        bool voxelMoveCaptured = false;
        if (!gizmoConsumesPointer && !selectionHandleCaptured &&
            !selectionBoxCaptured &&
            voxelToolState_.IsMoveActive() && selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Interior &&
            hoveredSelectionInterior && viewportRay && document &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const SelectionMovePlane movePlane = MakeSelectionMovePlane(
                hoveredSelectionInterior->WorldPosition,
                viewportRay->Direction);
            voxelMoveCaptured = selectionInteraction_.BeginMovingContent(
                selectionService_.EditableBounds(),
                voxelDocumentSession_.Generation(), movePlane,
                hoveredSelectionInterior->WorldPosition) &&
                transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation());
            if (!voxelMoveCaptured)
            {
                static_cast<void>(selectionInteraction_.Cancel());
                static_cast<void>(transformPreviewModel_.CancelPreview());
            }
            else
            {
                voxelMoveStatusMessage_.clear();
                UpdateVoxelHighlights();
            }
        }
        bool voxelDuplicateCaptured = false;
        if (!gizmoConsumesPointer && !selectionHandleCaptured &&
            !selectionBoxCaptured &&
            !voxelMoveCaptured && voxelToolState_.IsDuplicateActive() &&
            selectionInputAvailable &&
            selectionPointerTarget == SelectionPointerTarget::Interior &&
            hoveredSelectionInterior && viewportRay && document &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const SelectionMovePlane movePlane = MakeSelectionMovePlane(
                hoveredSelectionInterior->WorldPosition,
                viewportRay->Direction);
            voxelDuplicateCaptured =
                selectionInteraction_.BeginDuplicatingContent(
                    selectionService_.EditableBounds(),
                    voxelDocumentSession_.Generation(), movePlane,
                    hoveredSelectionInterior->WorldPosition) &&
                transformPreviewModel_.BeginPreview(
                    *document, selectionService_,
                    voxelDocumentSession_.Generation(), 0U,
                    TransformPreviewCollisionPolicy::IncludeSource);
            if (!voxelDuplicateCaptured)
            {
                static_cast<void>(selectionInteraction_.Cancel());
                static_cast<void>(transformPreviewModel_.CancelPreview());
            }
            else
            {
                voxelDuplicateStatusMessage_.clear();
                UpdateVoxelHighlights();
            }
        }
        if (!gizmoConsumesPointer && !selectionHandleCaptured &&
            !selectionBoxCaptured &&
            !voxelMoveCaptured && !voxelDuplicateCaptured &&
            (voxelToolState_.IsSelectionActive() ||
             voxelToolState_.ActiveTool() == ActiveVoxelTool::None) &&
            selectionInputAvailable &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            voxelSelectionClickCandidate_ = !cameraControl &&
                !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            selectionPointerAnchor_ = CurrentSelectionTarget();
            selectionPointerMode_ = io.KeyCtrl && io.KeyShift
                ? SelectionMode::Intersect
                : io.KeyCtrl ? SelectionMode::Subtract
                : io.KeyShift ? SelectionMode::Add
                              : SelectionMode::Replace;
            if (voxelSelectionClickCandidate_ &&
                voxelToolState_.IsSelectionActive())
            {
                if (selectionInteraction_.PointerDown(
                        selectionPointerAnchor_,
                        voxelDocumentSession_.Generation(),
                        selectionPointerMode_, io.MousePos.x, io.MousePos.y))
                    UpdateVoxelHighlights();
            }
        }
        if (selectionInteraction_.IsActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            std::optional<Asset::Voxel::VoxelDimensions> dimensions;
            if (document) dimensions = document->GetDimensions(0U);
            bool selectionChanged = false;
            if (selectionInteraction_.Mode() ==
                    SelectionInteractionMode::MovingBox &&
                viewportRay && dimensions)
            {
                const auto pointerWorld = IntersectSelectionMovePlane(
                    *viewportRay, selectionInteraction_.MovePlane());
                selectionChanged = pointerWorld &&
                    selectionInteraction_.MoveBox(
                        *pointerWorld, *dimensions);
            }
            else if ((selectionInteraction_.Mode() ==
                          SelectionInteractionMode::MovingContent ||
                      selectionInteraction_.Mode() ==
                          SelectionInteractionMode::DuplicatingContent) &&
                     viewportRay && document)
            {
                const auto pointerWorld = IntersectSelectionMovePlane(
                    *viewportRay, selectionInteraction_.MovePlane());
                selectionChanged = pointerWorld &&
                    selectionInteraction_.MoveContent(*pointerWorld);
                if (selectionChanged)
                {
                    Asset::Voxel::VoxelPosition delta =
                        selectionInteraction_.MoveDelta();
                    if (selectionInteraction_.Mode() ==
                        SelectionInteractionMode::MovingContent)
                        delta = ConstrainMoveDelta(delta);
                    static_cast<void>(transformPreviewModel_.SetDelta(
                        *document, selectionService_,
                        voxelDocumentSession_.Generation(),
                        delta));
                }
            }
            else
            {
                selectionChanged = selectionInteraction_.PointerMove(
                    io.MousePos.x, io.MousePos.y,
                    CurrentSelectionTarget(), dimensions);
            }
            if (selectionChanged)
            {
                if (selectionInteraction_.Mode() ==
                        SelectionInteractionMode::ResizingFace ||
                    selectionInteraction_.Mode() ==
                        SelectionInteractionMode::MovingBox)
                    static_cast<void>(ApplySelectionBounds(
                        selectionInteraction_.CurrentBounds(),
                        SelectionMode::Replace));
                else
                    UpdateVoxelHighlights();
            }
        }
        if (voxelSelectionClickCandidate_ &&
            (!sceneFocused || io.WantTextInput))
            voxelSelectionClickCandidate_ = false;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const TransformGizmoDragRelease gizmoRelease =
                transformGizmoManager_.EndInteraction();
            if (gizmoRelease.WasDragging && stampGizmoDragActive_)
            {
                stampGizmoDragActive_ = false;
                stampGizmoDragStartBounds_ = {};
                UpdateVoxelHighlights();
            }
            else if (gizmoRelease.WasDragging)
            {
                if (gizmoRelease.Mode == TransformGizmoMode::Rotate)
                {
                    if ((gizmoRelease.QuarterTurns % 4) == 0)
                        static_cast<void>(
                            transformPreviewModel_.CancelPreview());
                    else
                        static_cast<void>(ApplyVoxelRotate());
                }
                else if (gizmoRelease.Mode == TransformGizmoMode::Scale)
                {
                    if (gizmoRelease.Delta ==
                            Asset::Voxel::VoxelPosition{})
                        static_cast<void>(
                            transformPreviewModel_.CancelPreview());
                    else
                        static_cast<void>(ApplyVoxelScale());
                }
                else if (gizmoRelease.Delta ==
                             Asset::Voxel::VoxelPosition{})
                    static_cast<void>(transformPreviewModel_.CancelPreview());
                else
                    static_cast<void>(ApplyVoxelMove());
                UpdateVoxelHighlights();
            }
            else if (!stampPlacementSession_.IsActive())
            {
                const SelectionPointerRelease release =
                    selectionInteraction_.PointerUp();
                if (release.WasDrag && release.Bounds)
                {
                    if (release.Mode == SelectionInteractionMode::Creating)
                        static_cast<void>(ApplySelectionBounds(
                            *release.Bounds, release.Operation));
                    else if (release.Mode ==
                             SelectionInteractionMode::MovingContent)
                        static_cast<void>(ApplyVoxelMove());
                    else if (release.Mode ==
                             SelectionInteractionMode::DuplicatingContent)
                        static_cast<void>(ApplyVoxelDuplicate());
                    else
                        UpdateVoxelHighlights();
                }
                else
                {
                    bool changed = false;
                    if (voxelSelectionClickCandidate_ &&
                        voxelToolState_.IsSelectionActive())
                    {
                        if (const auto& hit = voxelSelection_.Hovered())
                        {
                            const Asset::Voxel::VoxelPosition position{
                                static_cast<std::int32_t>(hit->Coordinates.X),
                                static_cast<std::int32_t>(hit->Coordinates.Y),
                                static_cast<std::int32_t>(hit->Coordinates.Z)};
                            changed = selectionService_.Select(
                                position, selectionPointerMode_);
                        }
                        else if (!io.KeyCtrl && !io.KeyShift)
                        {
                            changed = selectionService_.Clear();
                        }
                    }
                    else if (voxelSelectionClickCandidate_)
                    {
                        changed = voxelSelection_.SelectHovered();
                    }
                    if (changed) UpdateVoxelHighlights();
                }
            }
            voxelSelectionClickCandidate_ = false;
            selectionPointerAnchor_.reset();
        }
        }
        if (imageHovered && !transformGizmoManager_.IsDragging())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                viewportNavigation_.Orbit(io.MouseDelta.x, io.MouseDelta.y);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                viewportNavigation_.Pan(
                    io.MouseDelta.x, io.MouseDelta.y, available.y);
            if (io.MouseWheel != 0.0F && !brushSizeInput.ConsumeWheel)
                viewportNavigation_.Zoom(io.MouseWheel);
        }
        const std::uint64_t feedbackNow = static_cast<std::uint64_t>(
            ImGui::GetTime() * 1000.0);
        spMarkSection(EditorFrameProbeSlot::SpPointer);
        // PERF-02a: aggregate previews carry no per-cell ghosts; the exact
        // statistics gate the label in both presentation modes.
        if (smartBrushGhostPreview_ != nullptr &&
            smartBrushGhostPreview_->Statistics.Total > 0U)
        {
            const SmartBrushState& state = toolContext_.Smart.Brush();
            const char* const shape = state.Shape == SmartBrushShape::Sphere
                ? "Sphere" : "Cube";
            const char* const action = toolContext_.Smart.Action() ==
                    SmartAction::Erase ? "Erase"
                : toolContext_.Smart.Action() == SmartAction::Paint
                    ? "Paint" : "Add";
            const SmartToolPlanStatistics& ghostStatistics =
                smartBrushGhostPreview_->Statistics;
            const std::string label = std::string(shape) + " / " + action +
                " / Size " + std::to_string(state.Size) + "\n" +
                "Total " + std::to_string(ghostStatistics.Total) +
                "  Affected " + std::to_string(ghostStatistics.Changed) +
                "  Ignored " + std::to_string(ghostStatistics.Unchanged) +
                "  Clipped " + std::to_string(ghostStatistics.Clipped);
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 minimum{imageOrigin.x + 12.0F,
                imageOrigin.y + available.y - textSize.y - 18.0F};
            const ImVec2 maximum{minimum.x + textSize.x + 16.0F,
                minimum.y + textSize.y + 12.0F};
            ImGui::GetWindowDrawList()->AddRectFilled(
                minimum, maximum, IM_COL32(8, 13, 20, 220), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(
                minimum, maximum, IM_COL32(105, 188, 238, 235), 4.0F);
            ImGui::GetWindowDrawList()->AddText(
                {minimum.x + 8.0F, minimum.y + 6.0F},
                IM_COL32(232, 244, 255, 255), label.c_str());
        }
        if (smartBrushSizeFeedback_.IsVisible(feedbackNow))
        {
            const char* const shape = smartBrushSizeFeedback_.Shape() ==
                    SmartBrushShape::Sphere ? "Sphere" : "Cube";
            const char* const action = smartBrushSizeFeedback_.Action() ==
                    SmartAction::Erase ? "Erase"
                : smartBrushSizeFeedback_.Action() == SmartAction::Paint
                    ? "Paint" : "Add";
            const std::string label = "Brush Size: " + std::to_string(
                smartBrushSizeFeedback_.Size()) + "\n" + shape +
                " - " + action;
            const ImVec2 minimum{imageOrigin.x + 12.0F, imageOrigin.y + 42.0F};
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 maximum{minimum.x + textSize.x + 16.0F,
                minimum.y + textSize.y + 12.0F};
            ImGui::GetWindowDrawList()->AddRectFilled(
                minimum, maximum, IM_COL32(8, 13, 20, 220), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(
                minimum, maximum, IM_COL32(70, 190, 235, 235), 4.0F);
            ImGui::GetWindowDrawList()->AddText(
                {minimum.x + 8.0F, minimum.y + 6.0F},
                IM_COL32(232, 244, 255, 255), label.c_str());
        }
        const bool sceneActive = imageHovered || sceneFocused;
        const bool shortcutsEnabled = sceneActive &&
            !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput &&
            !incompatiblePopupOpen;
        const std::uint8_t leftClickCount =
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                ? 2U
                : ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? 1U : 0U;
        const ViewportCameraActions cameraActions =
            ResolveViewportCameraActions({
                shortcutsEnabled,
                ImGui::IsKeyPressed(ImGuiKey_F, false),
                ImGui::IsKeyPressed(ImGuiKey_Home, false),
                leftClickCount});
        if (cameraActions.FocusRequested)
            FocusSelectionOrFrameAll();
        if (cameraActions.ResetRequested)
            viewportCamera_.Reset();
        if (shortcutsEnabled && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            eraseRequested = true;
        if (shortcutsEnabled && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_A, false))
            addRequested = true;
    }
    else if (!viewportRenderer_.LastError().empty())
    {
        voxelViewportRenderFailed_ = true;
        DrawErrorMessage(viewportRenderer_.LastError());
    }
    if (eraseRequested) static_cast<void>(EraseSelectedVoxel());
    if (addRequested) static_cast<void>(AddAdjacentVoxel());
    DrawFileDropOverlay(viewportDropRect_, DragDropImportTarget::Viewport);
    ImGui::End();
}

} // namespace VoxelForge::Editor
