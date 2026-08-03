// Highlights/preview presentation of EditorWorkspace (VF-0260 lot 7c).
// Pure code motion from EditorWorkspace.cpp.
#include "EditorWorkspace.h"
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
#include <string_view>
#include <sstream>
#include <span>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
// PERF-02e (option A, arbitrage Tony 02/08) : au-delà de ce nombre de voxels
// dans le document, la préview « état final exact » n'est plus composée — son
// coût est O(document) par cellule survolée. L'option B (compositeur
// incrémental, VF-0262) restaurera l'exactitude sans plafond.
constexpr std::uint64_t MaximumExactPreviewDocumentVoxelCount = 50'000U;
}

void EditorWorkspace::ForceVoxelHighlightsResolve() noexcept
{
    highlightsResolvedFrame_ = std::numeric_limits<std::uint64_t>::max();
    UpdateVoxelHighlights();
}

void EditorWorkspace::UpdateVoxelHighlights() noexcept
{
    // Lot 7b (PERF-02b) : une seule résolution complète par frame. Le premier
    // appel reste synchrone (les smokes agissent puis lisent dans la même
    // frame) ; les suivants sont différés et purgés au pré-rendu de la frame
    // suivante — le moment où le renderer consomme réellement cet état, comme
    // pour les appels post-rendu d'aujourd'hui.
    if (highlightsResolvedFrame_ == drawFrameIndex_)
    {
        highlightsUpdatePending_ = true;
        return;
    }
    highlightsResolvedFrame_ = drawFrameIndex_;
    highlightsUpdatePending_ = false;
    const EditorFrameProbeScope highlightsProbe(
        frameProbe_, EditorFrameProbeSlot::Highlights);
    // PERF-02d: section timestamps inside the function; each call attributes
    // the elapsed time since the previous mark to the given slot.
    auto hlSectionStart = std::chrono::steady_clock::now();
    const auto hlMarkSection =
        [this, &hlSectionStart](const EditorFrameProbeSlot slot)
    {
        const auto now = std::chrono::steady_clock::now();
        frameProbe_.Add(slot, std::chrono::duration<double, std::milli>(
            now - hlSectionStart).count());
        hlSectionStart = now;
    };
    universalCursor2DTarget_.reset();
    const auto coordinates = [](const std::optional<VoxelRaycastHit>& hit)
        -> std::optional<VoxelCoordinates>
    {
        return hit ? std::optional<VoxelCoordinates>(hit->Coordinates)
                   : std::nullopt;
    };
    std::optional<VoxelCoordinates> hoveredCoordinates =
        coordinates(voxelSelection_.Hovered());
    std::span<const Asset::Voxel::VoxelPosition> selectedCoordinates;
    std::array<Asset::Voxel::VoxelPosition, 1U> legacySelection{};
    std::optional<VoxelBoxBounds> selectionBounds;
    std::optional<SelectionBounds> editableSelectionBounds;
    const bool selectionVisualActive =
        voxelToolState_.IsSelectionActive() || voxelToolState_.IsMoveActive() ||
        voxelToolState_.IsDuplicateActive() || voxelToolState_.IsRotateActive() ||
        voxelToolState_.IsMirrorActive() || voxelToolState_.IsScaleActive() ||
        voxelToolState_.IsAlignActive();
    if (selectionVisualActive)
    {
        const auto selected = selectionService_.Voxels();
        const SelectionHighlightPlan highlightPlan =
            SelectionHighlightPolicy::Build(
                selected.size(), selectionInteraction_.IsActive());
        if (highlightPlan.DrawIndividualVoxels)
            selectedCoordinates = selected;
        const SelectionBounds& bounds = selectionService_.Bounds();
        if (bounds.Valid)
            selectionBounds = VoxelBoxBounds{bounds.Minimum, bounds.Maximum};
        const SelectionBounds& editable =
            (voxelToolState_.IsRotateActive() ||
             voxelToolState_.IsMirrorActive() ||
             voxelToolState_.IsScaleActive() ||
             voxelToolState_.IsAlignActive()) &&
                transformPreviewModel_.IsActive()
            ? transformPreviewModel_.PreviewBounds()
            :
            selectionInteraction_.IsActive() &&
            selectionInteraction_.IsDragRecognized()
            ? selectionInteraction_.CurrentBounds()
            : selectionService_.EditableBounds();
        if (editable.Valid) editableSelectionBounds = editable;
    }
    else if (const auto& selected = voxelSelection_.Selected())
    {
        legacySelection[0] = {
            static_cast<std::int32_t>(selected->Coordinates.X),
            static_cast<std::int32_t>(selected->Coordinates.Y),
            static_cast<std::int32_t>(selected->Coordinates.Z)};
        selectedCoordinates = legacySelection;
    }
    std::optional<Asset::Voxel::VoxelPosition> placementPosition;
    std::span<const Asset::Voxel::VoxelPosition> brushPreview;
    std::span<const Asset::Voxel::VoxelPosition> brushOccupiedPreview;
    std::optional<VoxelBoxBounds> brushAggregatePreview;
    std::optional<VoxelSpherePreview> brushAggregateSpherePreview;
    std::optional<VoxelBoxBounds> boxPreview;
    std::vector<Asset::Voxel::VoxelPosition> linePreview;
    std::optional<VoxelSpherePreview> spherePreview;
    std::span<const GhostVoxel> smartBrushGhostPreview;
    const SmartToolExactPreviewMesh* exactSmartToolPreview = nullptr;
    SmartToolPlanPtr exactSmartToolPlan;
    smartBrushGhostPreview_ = nullptr;
    VoxelPlacementPreviewStyle placementStyle =
        VoxelPlacementPreviewStyle::PencilInvalid;
    // Pencil V2 owns the Pencil preview when explicitly enabled.  Keep the
    // legacy Smart Tool presentation entirely dormant in that mode: having
    // both paths resolve the same hover would violate the one-preview rule.
    const bool pencilV2ToolActive = usePencilViewportInteractionV2_ &&
        voxelToolState_.IsPencilActive() && toolContext_.Smart.IsOperational() &&
        toolContext_.Smart.Geometry() == SmartGeometry::Pencil;
    const bool smartGeometryActive = !pencilV2ToolActive &&
        voxelToolState_.IsPencilActive() && toolContext_.Smart.IsOperational();
    const bool smartAddActive = smartGeometryActive &&
        toolContext_.Smart.Action() == SmartAction::Add;
    const bool smartPaintActive = smartGeometryActive &&
        toolContext_.Smart.Action() == SmartAction::Paint;
    const bool smartEraseActive = smartGeometryActive &&
        toolContext_.Smart.Action() == SmartAction::Erase;
    const bool faceAddPlanGhostPresentation =
        ShouldPresentFaceAddAsPlanGhosts(
            smartGeometryActive &&
                toolContext_.Smart.Geometry() == SmartGeometry::Face,
            smartAddActive,
            smartToolStroke_.IsActive());
    if (!smartAddActive && !smartEraseActive && !smartPaintActive)
        pencilPreviewCacheValid_ = false;
    if (!smartPaintActive && !voxelToolState_.IsFillActive())
        paintPreviewCacheValid_ = false;
    if (!smartAddActive && !smartEraseActive && !smartPaintActive &&
        !voxelToolState_.IsFillActive())
    {
        toolContext_.Smart.SetPreview(SmartToolPreviewState::Unavailable);
        toolContext_.Smart.ClearStatistics();
    }
    hlMarkSection(EditorFrameProbeSlot::HlPrep);
    if (smartAddActive || smartEraseActive || smartPaintActive)
    {
        const SmartToolStroke* const activeStroke = smartToolStroke_.IsActive()
            ? &smartToolStroke_ : nullptr;
        SmartToolPlanPtr plan;
        if (ShouldResolvePreviewForPresentation(activeStroke != nullptr))
        {
            const EditorFrameProbeScope previewProbe(
                frameProbe_, EditorFrameProbeSlot::PreviewResolve);
            const std::optional<SmartToolRequest> request =
                BuildSmartPencilRequest();
            const SmartToolResult planning = request
                ? smartToolController_.ResolvePreview(
                    smartToolSession_, *request)
                : SmartToolResult{};
            plan = planning.Plan;
        }
        else
        {
            // ContinueSmartToolStroke already resolved and accepted this
            // immutable plan. Replanning from the raw hover here would make
            // the cursor outrun the actual stroke and duplicate expensive
            // planner work on every pointer update.
            plan = smartToolStrokePreviewPlan_;
        }
        const std::optional<Asset::Voxel::VoxelPosition> anchor = plan != nullptr
            ? std::optional<Asset::Voxel::VoxelPosition>{plan->Placement().Target}
            : std::nullopt;
        if (plan != nullptr)
        {
            // This engine consumes only the materialized immutable plan. It
            // never asks a document, palette, or planner for another value.
            smartBrushGhostPreview_ = &smartPreviewCache_.Resolve(plan);
            const bool aggregateSmartPreview =
                smartBrushGhostPreview_->RenderPlan.Mode !=
                SmartBrushRenderMode::DetailedCells;
            const Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            const bool exactPreviewAffordable = document != nullptr &&
                document->GetVoxelCount() <=
                    MaximumExactPreviewDocumentVoxelCount;
            if (document != nullptr)
            {
                exactSmartToolPlan = plan;
                if (aggregateSmartPreview || !exactPreviewAffordable)
                {
                    // PERF-02a: above MaximumDetailedBrushPreviewVoxelCount
                    // the plan presents aggregate bounds. Composing the exact
                    // final-state mesh would cost O(volume) per pointer
                    // update; the aggregate outline plus exact statistics
                    // stand in for it.
                    smartToolExactPreviewCache_.Clear();
                }
                else if (faceAddPlanGhostPresentation)
                {
                    // Face depth replaces the stroke with one complete,
                    // immutable plan. Present those exact cells directly:
                    // rebuilding a private copy of the entire document and
                    // remeshing it on every depth step is unnecessary.
                }
                else if (activeStroke != nullptr)
                {
                    const bool rebuild = smartToolStrokePreviewPlanId_ !=
                            plan->PlanId() ||
                        smartToolStrokePreviewPlanRevision_ != plan->Revision() ||
                        smartToolStrokePreviewStrokeRevision_ !=
                            activeStroke->Revision();
                    if (rebuild)
                    {
                        smartToolStrokePreviewMesh_ =
                            SmartToolExactPreviewComposer::Compose(*document,
                                activeStroke->Changes());
                        smartToolStrokePreviewPlanId_ = plan->PlanId();
                        smartToolStrokePreviewPlanRevision_ = plan->Revision();
                        smartToolStrokePreviewStrokeRevision_ =
                            activeStroke->Revision();
                    }
                    exactSmartToolPreview = &smartToolStrokePreviewMesh_;
                }
                else
                {
                    exactSmartToolPreview = &smartToolExactPreviewCache_.Resolve(
                        *document, voxelDocumentSession_.Generation(), plan);
                }
            }
        }
        else
        {
            if (activeStroke == nullptr) smartToolSession_.Clear();
            smartToolExactPreviewCache_.Clear();
            // Keep the accumulated exact state visible while a target is
            // temporarily invalid. The stroke remains suspended and the next
            // valid target starts a fresh segment; no missing target is ever
            // interpolated across.
            const Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            const bool invalidReplacementEndpoint =
                (smartLineLockedStart_ && !smartLineEndpointValid_) ||
                (smartGeometryPlane_ && !smartGeometryEndpointValid_);
            // PERF-02d: aggregate strokes never compose the exact accumulated
            // mesh (O(stroke volume) per revision, measured 86-135 ms during
            // suspended-target moments of large drags); their presentation
            // stays the aggregate outline, matching the primary stroke path.
            if (!invalidReplacementEndpoint && activeStroke != nullptr && document != nullptr &&
                document->GetVoxelCount() <=
                    MaximumExactPreviewDocumentVoxelCount &&
                smartToolStrokePreviewPlan_ != nullptr &&
                smartToolStrokePreviewPlan_->BrushResult().RenderPlan.Mode ==
                    SmartBrushRenderMode::DetailedCells)
            {
                exactSmartToolPlan = smartToolStrokePreviewPlan_;
                if (smartToolStrokePreviewStrokeRevision_ !=
                    activeStroke->Revision())
                {
                    smartToolStrokePreviewMesh_ =
                        SmartToolExactPreviewComposer::Compose(*document,
                            activeStroke->Changes());
                    smartToolStrokePreviewStrokeRevision_ =
                        activeStroke->Revision();
                }
                exactSmartToolPreview = &smartToolStrokePreviewMesh_;
            }
        }
        hlMarkSection(EditorFrameProbeSlot::HlSmartPlan);
        voxelPlacementPreview_ = {};
        if (smartBrushGhostPreview_ != nullptr)
        {
            const SmartPreviewData& preview = *smartBrushGhostPreview_;
            if (faceAddPlanGhostPresentation)
                smartBrushGhostPreview = preview.GhostVoxels;
            voxelPlacementPreview_.Tool = smartEraseActive
                ? VoxelPreviewTool::Eraser : VoxelPreviewTool::Pencil;
            voxelPlacementPreview_.Position = anchor;
            voxelPlacementPreview_.RenderPlan = preview.RenderPlan;
            voxelPlacementPreview_.Statistics = {preview.Statistics.Total,
                smartEraseActive ? preview.Statistics.Unchanged :
                    preview.Statistics.Changed,
                smartEraseActive ? preview.Statistics.Changed :
                    preview.Statistics.Unchanged,
                preview.Statistics.Clipped};
            for (const GhostVoxel& ghost : preview.GhostVoxels)
            {
                if (ghost.State == GhostVoxelState::Added ||
                    ghost.State == GhostVoxelState::Painted ||
                    (smartEraseActive && ghost.State == GhostVoxelState::Ignored))
                    voxelPlacementPreview_.AddablePositions.push_back(ghost.Position);
                else if (ghost.State == GhostVoxelState::Erased ||
                    (!smartEraseActive && ghost.State == GhostVoxelState::Ignored))
                    voxelPlacementPreview_.OccupiedPositions.push_back(ghost.Position);
                if (ghost.State != GhostVoxelState::Clipped &&
                    ghost.State != GhostVoxelState::Invalid)
                    voxelPlacementPreview_.Positions.push_back(ghost.Position);
            }
            voxelPlacementPreview_.Status = preview.Code ==
                    SmartBrushResultCode::OutOfBounds
                ? VoxelPlacementPreviewStatus::OutOfBounds
                : preview.Code == SmartBrushResultCode::Valid
                ? preview.Statistics.Changed == 0U
                    ? VoxelPlacementPreviewStatus::Occupied
                    : VoxelPlacementPreviewStatus::Valid
                : VoxelPlacementPreviewStatus::Unavailable;
            toolContext_.Smart.SetStatistics(preview.Statistics.Total,
                preview.Statistics.Changed, preview.Statistics.Unchanged,
                preview.Statistics.Clipped);
            toolContext_.Smart.SetPreview(preview.Code ==
                    SmartBrushResultCode::OutOfBounds
                ? SmartToolPreviewState::OutOfBounds
                : preview.Code != SmartBrushResultCode::Valid
                ? SmartToolPreviewState::Unavailable
                : preview.Statistics.Changed == 0U
                ? SmartToolPreviewState::NoChange : SmartToolPreviewState::Valid,
                preview.RenderPlan);
        }
        else
        {
            toolContext_.Smart.SetPreview(SmartToolPreviewState::Unavailable);
            toolContext_.Smart.ClearStatistics();
        }
        placementStyle = smartEraseActive ? VoxelPlacementPreviewStyle::Eraser :
            voxelPlacementPreview_.Status == VoxelPlacementPreviewStatus::Valid
            ? VoxelPlacementPreviewStyle::PencilValid
            : voxelPlacementPreview_.Status == VoxelPlacementPreviewStatus::Occupied
            ? VoxelPlacementPreviewStyle::PencilOccupied
            : VoxelPlacementPreviewStyle::PencilInvalid;
    }
    else if (voxelToolState_.IsEraserActive())
    {
        const VoxelPickingInteractionState interaction =
            voxelSelection_.InteractionState();
        const bool blocked =
            interaction == VoxelPickingInteractionState::CameraInteraction ||
            interaction == VoxelPickingInteractionState::Blocked;
        voxelPlacementPreview_ = EvaluateVoxelEraserPreview(
            voxelDocumentSession_.ActiveDocument(),
            0U,
            voxelSelection_.Hovered(),
            true,
            blocked);
        placementPosition = voxelPlacementPreview_.IsVisible()
            ? voxelPlacementPreview_.Position : std::nullopt;
        placementStyle = VoxelPlacementPreviewStyle::Eraser;
        if (placementPosition) hoveredCoordinates.reset();
    }
    else if (voxelToolState_.IsFillActive())
    {
        Asset::Voxel::VoxelDocument* document =
            voxelDocumentSession_.ActiveDocument();
        SmartBrushState previewState = toolContext_.Smart.Brush();
        previewState.Shape = ResolveSmartBrushShape(
            toolContext_.Smart.Geometry(), previewState.Shape);
        previewState.Mode = SmartBrushMode::Paint;
        if (const std::optional<PaletteColorSelection> activeColor =
                paletteService_.ActiveColor())
            toolContext_.Smart.Brush().PaletteIndex = activeColor->Index;
        else
            toolContext_.Smart.Brush().PaletteIndex = 0U;
        previewState.PaletteIndex = toolContext_.Smart.Brush().PaletteIndex;
        const std::optional<VoxelRaycastHit>& hit = voxelSelection_.Hovered();
        const std::optional<VoxelCoordinates> hitCoordinates = hit
            ? std::optional<VoxelCoordinates>{hit->Coordinates}
            : std::nullopt;
        const VoxelHitFace face = hit ? hit->Face : VoxelHitFace::None;
        const std::size_t hitSubModelIndex = hit ? hit->SubModelIndex : 0U;
        const std::uint64_t revision = document ? document->GetRevision() : 0U;
        const std::uint64_t generation = voxelDocumentSession_.Generation();
        voxelPlacementPreview_ = {};
        if (!paintPreviewCacheValid_ || paintPreviewDocument_ != document ||
            paintPreviewRevision_ != revision ||
            paintPreviewGeneration_ != generation ||
            paintPreviewCoordinates_ != hitCoordinates ||
            paintPreviewFace_ != face ||
            paintPreviewHitSubModelIndex_ != hitSubModelIndex ||
            paintPreviewState_ != previewState)
        {
            try
            {
                paintPreviewEvaluation_ = VoxelPaintBrushTool::Evaluate({
                    nullptr, document, 0U, hit, previewState, false, nullptr});
            }
            catch (...)
            {
                paintPreviewEvaluation_ = {};
            }
            paintPreviewDocument_ = document;
            paintPreviewRevision_ = revision;
            paintPreviewGeneration_ = generation;
            paintPreviewCoordinates_ = hitCoordinates;
            paintPreviewFace_ = face;
            paintPreviewHitSubModelIndex_ = hitSubModelIndex;
            paintPreviewState_ = previewState;
            paintPreviewCacheValid_ = true;
        }
        toolContext_.Smart.SetStatistics(
            paintPreviewEvaluation_.Statistics.Total,
            paintPreviewEvaluation_.Statistics.Painted,
            paintPreviewEvaluation_.Statistics.Ignored,
            paintPreviewEvaluation_.Statistics.Clipped);
        toolContext_.Smart.SetPreview(
            paintPreviewEvaluation_.Code == VoxelPaintBrushResultCode::Applied
                ? SmartToolPreviewState::Valid
                : paintPreviewEvaluation_.Code ==
                    VoxelPaintBrushResultCode::NoChange
                ? SmartToolPreviewState::NoChange
                : paintPreviewEvaluation_.Code ==
                    VoxelPaintBrushResultCode::TargetOutOfBounds
                ? SmartToolPreviewState::OutOfBounds
                : SmartToolPreviewState::Unavailable,
            paintPreviewEvaluation_.RenderPlan);
        if (paintPreviewEvaluation_.IsResolved())
        {
            brushPreview = paintPreviewEvaluation_.PaintablePositions;
            brushOccupiedPreview = paintPreviewEvaluation_.IgnoredPositions;
            placementStyle = paintPreviewEvaluation_.Code ==
                    VoxelPaintBrushResultCode::Applied
                ? VoxelPlacementPreviewStyle::PencilValid
                : VoxelPlacementPreviewStyle::PencilOccupied;
        }
    }
    else if (voxelToolState_.IsBoxActive())
    {
        voxelPlacementPreview_ = {};
        if (const auto* document = voxelDocumentSession_.ActiveDocument();
            document && voxelBoxInteraction_.CornerA() &&
            voxelBoxInteraction_.CornerB())
        {
            boxPreview = VoxelBoxService::CalculateBounds(
                *document, 0U, *voxelBoxInteraction_.CornerA(),
                *voxelBoxInteraction_.CornerB());
        }
    }
    else if (voxelToolState_.IsLineActive())
    {
        voxelPlacementPreview_ = {};
        if (const auto* document = voxelDocumentSession_.ActiveDocument();
            document && voxelLineInteraction_.PointA() &&
            voxelLineInteraction_.PointB())
        {
            try
            {
                linePreview = VoxelLineService::CalculatePositions(
                    *document, 0U, *voxelLineInteraction_.PointA(),
                    *voxelLineInteraction_.PointB());
            }
            catch (...)
            {
                linePreview.clear();
            }
        }
    }
    else if (voxelToolState_.IsSphereActive())
    {
        voxelPlacementPreview_ = {};
        if (voxelSphereInteraction_.Center() &&
            voxelSphereInteraction_.RadiusPoint())
        {
            spherePreview = VoxelSpherePreview{
                *voxelSphereInteraction_.Center(),
                VoxelSphereService::CalculateRadius(
                    *voxelSphereInteraction_.Center(),
                    *voxelSphereInteraction_.RadiusPoint())};
        }
    }
    else
    {
        voxelPlacementPreview_ = {};
        const Voxel::VoxelGrid* grid = activeVoxelModel_
            ? activeVoxelModel_->GetGrid(0U) : nullptr;
        const AddVoxelTarget addTarget =
            FindAddVoxelTarget(grid, voxelSelection_.Selected());
        if (addTarget)
        {
            placementPosition = Asset::Voxel::VoxelPosition{
                static_cast<std::int32_t>(addTarget.Coordinates->X),
                static_cast<std::int32_t>(addTarget.Coordinates->Y),
                static_cast<std::int32_t>(addTarget.Coordinates->Z)};
            placementStyle = VoxelPlacementPreviewStyle::PencilValid;
        }
    }
    if (pencilV2ToolActive)
    {
        // Presentation LOD only: the immutable compact plans remain the exact
        // commit source.  Small footprints are rendered as exact cells;
        // larger ones never expand into millions of CPU ghost voxels.
        const InteractionV2::PencilCompactPresentation& presentation =
            pencilViewportInteractionV2_.Presentation();
        if (pencilV2RenderedPresentationRevision_ != presentation.Revision)
        {
            pencilV2PreviewPositions_.clear();
            const Asset::Voxel::VoxelDocument* const document =
                voxelDocumentSession_.ActiveDocument();
            if (document && presentation.Detail ==
                    InteractionV2::PencilPreviewDetail::Exact)
            {
                // Presentation and commit both consume this pure shared
                // resolver.  Paint/Erase/overlap previews therefore expose
                // exactly the resulting cells, never the raw footprint.
                const auto resolved = InteractionV2::PencilCompactChangeResolver::
                    Resolve(*document, voxelDocumentSession_.Generation(),
                        presentation.Plans);
                if (resolved)
                {
                    pencilV2PreviewPositions_.reserve(resolved->size());
                    for (const VoxelChange& change : *resolved)
                        pencilV2PreviewPositions_.push_back(change.Position);
                }
            }
            pencilV2RenderedPresentationRevision_ = presentation.Revision;
        }
        if (!pencilV2PreviewPositions_.empty())
            brushPreview = pencilV2PreviewPositions_;
        else if (presentation.Bounds &&
                 (presentation.Detail ==
                      InteractionV2::PencilPreviewDetail::CompactDeferred ||
                  presentation.Validity ==
                      InteractionV2::PencilPreviewValidity::OutOfBounds))
            brushAggregatePreview = VoxelBoxBounds{
                presentation.Bounds->Minimum, presentation.Bounds->Maximum};
        placementPosition.reset();
        placementStyle = presentation.Validity ==
                InteractionV2::PencilPreviewValidity::OutOfBounds
            ? VoxelPlacementPreviewStyle::PencilInvalid
            : presentation.Detail ==
                    InteractionV2::PencilPreviewDetail::CompactDeferred
            ? VoxelPlacementPreviewStyle::PencilOccupied
            : presentation.Detail == InteractionV2::PencilPreviewDetail::Exact &&
                    pencilV2PreviewPositions_.empty()
            ? VoxelPlacementPreviewStyle::PencilOccupied
            : VoxelPlacementPreviewStyle::PencilValid;
    }
    const UniversalCursorPreviewSubject previewSubject =
        (smartGeometryActive || pencilV2ToolActive) &&
            toolContext_.Smart.Geometry() == SmartGeometry::Pencil
        ? toolContext_.Smart.Mode() == SmartToolMode::SingleVoxel
            ? UniversalCursorPreviewSubject::PencilSingleVoxel
            : UniversalCursorPreviewSubject::PencilBrush
        : UniversalCursorPreviewSubject::Geometric;
    hlMarkSection(EditorFrameProbeSlot::HlTools);
    const bool universalCursorToolActive = smartGeometryActive ||
        pencilV2ToolActive || voxelToolState_.IsFillActive();
    if (universalCursorToolActive)
    {
        std::optional<UniversalCursor2DTarget> hoveredCursorTarget;
        const std::optional<VoxelRaycastHit>& hit = voxelSelection_.Hovered();
        if (hit && hit->Face != VoxelHitFace::None)
        {
            hoveredCursorTarget = MakeVoxelFaceCursor2DTarget(
                {static_cast<std::int32_t>(hit->Coordinates.X),
                 static_cast<std::int32_t>(hit->Coordinates.Y),
                 static_cast<std::int32_t>(hit->Coordinates.Z)},
                VoxelHitFaceIntegerNormal(hit->Face), voxelModelCenter_);
        }

        const SmartToolPlan* plannedCursorPlan = exactSmartToolPlan.get();
        const bool activeStrokeCursor = smartToolStroke_.IsActive();
        const bool lockedFaceStroke = activeStrokeCursor &&
            toolContext_.Smart.Geometry() == SmartGeometry::Face &&
            faceDepthLockedSeed_.has_value();
        if (plannedCursorPlan == nullptr && activeStrokeCursor &&
            smartToolStrokePreviewPlan_ != nullptr)
            plannedCursorPlan = smartToolStrokePreviewPlan_.get();
        std::optional<UniversalCursor2DTarget> plannedCursorTarget;
        if (plannedCursorPlan != nullptr)
        {
            const SmartBrushPlacement& placement =
                plannedCursorPlan->Placement();
            Asset::Voxel::VoxelPosition cursorVoxel = placement.Target;
            if (lockedFaceStroke && faceDepthLockedSeed_)
            {
                cursorVoxel = faceDepthLockedSeed_->Position;
                if (plannedCursorPlan->Action() == SmartAction::Add)
                {
                    const Asset::Voxel::VoxelPosition normal =
                        faceDepthLockedSeed_->Normal;
                    const auto presentedTarget =
                        MakeOutermostVoxelFaceCursor2DTarget(
                            smartBrushGhostPreview_ != nullptr
                                ? std::span<const Asset::Voxel::VoxelPosition>{
                                    smartBrushGhostPreview_->AffectedPositions}
                                : std::span<const Asset::Voxel::VoxelPosition>{
                                    plannedCursorPlan->AffectedPositions()},
                            faceDepthLockedSeed_->Position,
                            normal, voxelModelCenter_);
                    if (presentedTarget)
                        plannedCursorTarget = *presentedTarget;
                }
                if (!plannedCursorTarget)
                    plannedCursorTarget = MakeVoxelFaceCursor2DTarget(
                        cursorVoxel, faceDepthLockedSeed_->Normal,
                        voxelModelCenter_);
            }
            else
            {
                if (plannedCursorPlan->Action() == SmartAction::Add)
                {
                    cursorVoxel.X -= placement.Normal.X;
                    cursorVoxel.Y -= placement.Normal.Y;
                    cursorVoxel.Z -= placement.Normal.Z;
                }
                plannedCursorTarget = MakeVoxelFaceCursor2DTarget(
                    cursorVoxel, placement.Normal, voxelModelCenter_);
            }
        }
        universalCursor2DTarget_ = SelectUniversalCursor2DTarget(
            hoveredCursorTarget, plannedCursorTarget,
            activeStrokeCursor
                ? UniversalCursorAnchorPolicy::PreferPlannedTarget
                : UniversalCursorAnchorPolicy::PreferHoveredTarget);
    }
    if (universalCursorToolActive)
    {
        // The universal cursor replaces only the legacy volumetric hover
        // marker. Tool geometry remains an independent preview layer.
        hoveredCoordinates.reset();
    }
    // Smart Add/Erase/Paint render exclusively from the immutable planner
    // output through the exact final-state preview.
    if (exactSmartToolPreview == nullptr) smartToolExactPreviewCache_.Clear();
    if (smartAddActive || smartEraseActive || smartPaintActive)
    {
        brushPreview = {};
        brushOccupiedPreview = {};
        brushAggregatePreview.reset();
        brushAggregateSpherePreview.reset();
        if (smartBrushGhostPreview_ != nullptr)
        {
            // PERF-02a: large brushes ship an aggregate render plan; present
            // its bounds through the legacy aggregate channels instead of
            // per-cell ghosts and the exact final-state mesh.
            const SmartBrushRenderPlan& renderPlan =
                smartBrushGhostPreview_->RenderPlan;
            if (renderPlan.Mode == SmartBrushRenderMode::AggregateSphere)
                brushAggregateSpherePreview = VoxelSpherePreview{
                    renderPlan.SphereCenter, renderPlan.SphereRadius};
            else if (renderPlan.Mode == SmartBrushRenderMode::AggregateBox)
                brushAggregatePreview = VoxelBoxBounds{
                    renderPlan.Bounds.Minimum, renderPlan.Bounds.Maximum};
        }
    }
    if (exactSmartToolPreview != nullptr && exactSmartToolPreview->Succeeded())
    {
        // The exact final-state mesh replaces the base model for this frame;
        // legacy hover/selection highlights would otherwise falsely describe
        // the pre-commit document, most visibly for Remove.
        hoveredCoordinates.reset();
        selectedCoordinates = {};
        selectionBounds.reset();
        editableSelectionBounds.reset();
    }
    hlMarkSection(EditorFrameProbeSlot::HlCursor);
    const EditorFrameProbeScope handoffProbe(
        frameProbe_, EditorFrameProbeSlot::HighlightsHandoff);
    viewportRenderer_.ConfigureHighlights(
        hoveredCoordinates,
        selectedCoordinates,
        selectionBounds,
        editableSelectionBounds,
        selectionVisualActive,
        (selectionInteraction_.Mode() == SelectionInteractionMode::MovingBox ||
         selectionInteraction_.Mode() == SelectionInteractionMode::MovingContent ||
         selectionInteraction_.Mode() ==
             SelectionInteractionMode::DuplicatingContent)
            ? SelectionBoxVisualState::Moving
            : selectionBoxInteriorHovered_
            ? SelectionBoxVisualState::Hovered
            : SelectionBoxVisualState::Normal,
        placementPosition,
        placementStyle,
        brushPreview,
        brushOccupiedPreview,
        brushAggregatePreview,
        brushAggregateSpherePreview,
        boxPreview,
        linePreview,
        spherePreview,
        smartBrushGhostPreview,
        faceAddPlanGhostPresentation
            ? SmartBrushGhostGeometryStyle::ExposedFaceSurface
            : SmartBrushGhostGeometryStyle::VoxelBoxes,
        voxelModelCenter_);
    const Asset::Voxel::VoxelDocument* const activeDocument =
        voxelDocumentSession_.ActiveDocument();
    if (ShouldRenderExactPreviewGeometry(
            previewSubject, smartToolStroke_.IsActive()) &&
        exactSmartToolPreview != nullptr && exactSmartToolPlan != nullptr &&
        exactSmartToolPreview->Succeeded())
    {
        static_cast<void>(viewportRenderer_.ConfigureExactPreviewMesh(
            &exactSmartToolPreview->Mesh, &exactSmartToolPreview->Palette,
            voxelModelCenter_, exactSmartToolPreview->Active,
            voxelDocumentSession_.Generation(), activeDocument
                ? activeDocument->GetRevision() : 0U,
            exactSmartToolPlan->PlanId(), smartToolStroke_.IsActive()
                ? smartToolStroke_.Revision() : exactSmartToolPlan->Revision()));
    }
    else if (!ShouldRetainExactPreviewOnMissingFrame(
                 previewSubject, smartToolStroke_.IsActive()))
    {
        static_cast<void>(viewportRenderer_.ConfigureExactPreviewMesh(
            nullptr, nullptr, {}, false, 0U, 0U, 0U, 0U));
    }
    viewportRenderer_.ConfigureVoxelPreview(
        stampPlacementSession_.CurrentPreview());
    if (activeDocument && transformPreviewModel_.IsValidFor(
            *activeDocument, selectionService_, voxelDocumentSession_.Generation()))
    {
        const TransformPreviewRenderData preview =
            transformPreviewModel_.RenderData();
        viewportRenderer_.ConfigureTransformPreview(&preview);
    }
    else
    {
        viewportRenderer_.ConfigureTransformPreview(nullptr);
    }
}

} // namespace VoxelForge::Editor
