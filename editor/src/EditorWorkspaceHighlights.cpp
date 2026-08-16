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
#include <cstdlib>
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
// ERGO-01 LOT 0 (06/08/2026) : le plafond portait sur la taille du DOCUMENT
// (50 000 voxels), parce que la composition etait alors en O(document). Depuis
// VF-0265 et le LOT 4c elle ne l'est plus, et la mesure montre que ce plafond
// est non seulement inutile mais CONTRE-PRODUCTIF : il compte une quantite sans
// rapport avec le cout.
//
// Mesure du 06/08/2026 (build\ergo01-lot0.csv, sans assemblage monolithique,
// comme l'editeur reel). Cout de composition pour un delta de 27 voxels :
//   dense 1 000 000 voxels ......... 0,053 ms a froid, 0,007 ms a chaud
//   dense    15 625 voxels ......... 0,484 ms a froid, 0,121 ms a chaud
//   sparse10k 10 000 voxels en 64 .. 2,283 ms a froid, 0,453 ms a chaud
// Un million de voxels denses coute donc DIX FOIS MOINS que quinze mille, et
// quarante fois moins que dix mille epars. Le plafond a 50 000 laissait passer
// le cas lent et bloquait le cas rapide.
//
// Raison : le cout est domine par le nombre de FACES du chunk touche, borne par
// le chunk lui-meme (32 cube), et non par la taille du document. Un chunk au
// coeur d'un modele dense n'a presque aucune face, tout etant masque par ses
// voisins ; des voxels epars en exposent au maximum.
//
// La seule quantite qui gouverne le cout est donc le VOLUME DU DELTA. Courbe
// mesuree, p95 du pire scenario, a froid :
//     27 voxels ->  4,00 ms      729 ->  4,67 ms
//  4 913 voxels ->  6,69 ms   35 937 -> 31,92 ms   (hors budget)
// Le basculement est entre 5 000 et 36 000. Le seuil est pose a 8 192, ce qui
// maintient le pire cas mesure autour de 10 ms et laisse la marge du reste de
// la frame. Les gros pinceaux sont de toute facon deja exclus en amont par
// aggregateSmartPreview : ce plafond ne protege que les traits qui accumulent.
constexpr std::size_t MaximumExactPreviewDeltaVoxelCount = 8'192U;
}

SmartToolExactPreviewComposer::Source EditorWorkspace::ExactPreviewSource(
    const Asset::Voxel::VoxelDocument& document) const noexcept
{
    SmartToolExactPreviewComposer::Source source;
    source.Document = &document;
    source.ModelIndex = 0U;

    // VF-0265 : interrupteur de diagnostic. Avec VOXELFORGE_LEGACY_EXACT_PREVIEW
    // defini, on rend exactement le comportement d'avant le chantier — copie du
    // document, remaillage complet, mesh unique monolithique. C'est le seul
    // moyen d'attribuer un symptome ressenti (ici un decrochage du curseur au
    // survol rapide) au nouveau chemin ou a autre chose, sans deviner.
    // Lu une seule fois : ce n'est pas un reglage produit.
    static const bool legacyExactPreview = []
    {
        const char* const value = std::getenv("VOXELFORGE_LEGACY_EXACT_PREVIEW");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    if (legacyExactPreview)
    {
        source.DocumentChunks = nullptr;
        source.AssembleMesh = true;
        return source;
    }
    // Le cache de chunks décrit le document SANS le plan. Il n'est exploitable
    // que s'il décrit bien CE document, à CETTE révision, pour CE sous-modèle.
    // SynchronizeVoxelDocumentRendering tourne en tête de frame, donc le cas
    // normal est satisfait ; les exceptions sont le chargement (upload
    // monolithique, cache encore vide) et un cache invalidé. Dans ces cas on
    // laisse DocumentChunks à nullptr et le compositeur reprend son chemin de
    // référence : plus lent, mais jamais faux.
    const auto identity = voxelDocumentMeshCache_.DocumentIdentity();
    const auto revision = voxelDocumentMeshCache_.DocumentRevision();
    if (identity && revision &&
        *identity == voxelDocumentSession_.Generation() &&
        *revision == document.GetRevision() &&
        voxelDocumentMeshCache_.ModelIndex() == 0U &&
        !voxelDocumentMeshCache_.Chunks().empty())
    {
        source.DocumentChunks = &voxelDocumentMeshCache_.Chunks();
    }
    // VF-0265 (lot 3h) : l'assemblage du mesh unique est le dernier poste qui
    // suit la taille du document — 8 Mo de tampons à un million de voxels. On ne
    // le paie que si le renderer ne peut PAS consommer les chunks : sinon le
    // repli monolithique présenterait un mesh vide, et la preview disparaîtrait
    // au lieu d'être seulement plus lente.
    source.AssembleMesh =
        source.DocumentChunks == nullptr ||
        viewportRenderer_.ModelChunkCount() == 0U;
    // LOT 4c : cache d'overrides par chunk. Seul le chemin chunke en profite ;
    // le chemin de reference reste sans etat, donc utilisable comme oracle.
    if (source.DocumentChunks != nullptr)
        source.ChunkCache = &exactPreviewChunkCache_;
    return source;
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
        // LATENCE-01 : on retient la frame de la PREMIERE demande differee.
        // C'est elle qui mesure le retard reellement subi ; les demandes
        // suivantes de la meme frame ne l'aggravent pas.
        if (!highlightsUpdatePending_)
            highlightsDeferredAtFrame_ = drawFrameIndex_;
        highlightsUpdatePending_ = true;
        return;
    }
    // LATENCE-01 : combien de frames se sont ecoulees entre la demande et cette
    // resolution. Zero est l'objectif ; tout ce qui est au-dessus est du retard
    // que l'utilisateur voit comme un curseur qui prend de l'avance sur le
    // surlignage. Le debit ne le montre pas : la session du 06/08 tenait 60 FPS
    // avec 0,3 % de frames hors budget, et la desynchro ressentie etait la meme.
    if (highlightsUpdatePending_ && drawFrameIndex_ >= highlightsDeferredAtFrame_)
    {
        frameProbe_.NoteHighlightLatency(
            drawFrameIndex_ - highlightsDeferredAtFrame_);
    }
    else
    {
        frameProbe_.NoteHighlightLatency(0U);
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
        voxelToolState_.IsAlignActive() ||
        voxelToolState_.IsWrapActive();
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
            // VF-WRAP-V1 : pendant la preview Wrap, les guides et poignees
            // representent les bornes DEMANDEES par le geste — la face que
            // l'utilisateur manipule — jamais les bornes serrees du resultat.
            voxelToolState_.IsWrapActive() &&
                transformPreviewModel_.IsActive() && WrapTargetBounds().Valid
            ? WrapTargetBounds()
            :
            (voxelToolState_.IsRotateActive() ||
             voxelToolState_.IsMirrorActive() ||
             voxelToolState_.IsScaleActive() ||
             voxelToolState_.IsAlignActive() ||
             voxelToolState_.IsWrapActive()) &&
                transformPreviewModel_.IsActive()
            ? transformPreviewModel_.PreviewBounds()
            :
            selectionInteraction_.IsActive() &&
            selectionInteraction_.IsDragRecognized()
            // PREVIEW == COMMIT : le geste de CREATION passe par la meme regle
            // que le relachement (ResolveSelectionGestureBounds). En mode Rect
            // la preview est donc deja aplatie a une couche pendant le drag —
            // exactement ce que le commit appliquera. Les autres gestes
            // (redimensionnement, deplacement) montrent leurs bornes brutes.
            ? (selectionInteraction_.Mode() ==
                       SelectionInteractionMode::Creating
                   ? ResolveSelectionGestureBounds(
                         toolContext_.Selection.Mode,
                         selectionInteraction_.CurrentBounds(),
                         selectionRectAxis_, selectionRectLayer_)
                   : selectionInteraction_.CurrentBounds())
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
    const VoxelPlacementPreview* smartBrushPlacementPreview = nullptr;
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
            // ERGO-01 LOT 0 : la garde porte sur le DELTA, pas sur le
            // document. En cours de trait c'est le nombre de changements
            // accumules ; au survol, le nombre de cellules du plan.
            const std::size_t exactPreviewDeltaVoxels = activeStroke != nullptr
                ? activeStroke->ChangesView().size()
                : plan->Cells().size();
            const bool exactPreviewAffordable = document != nullptr &&
                exactPreviewDeltaVoxels <= MaximumExactPreviewDeltaVoxelCount;
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
                            SmartToolExactPreviewComposer::Compose(
                                ExactPreviewSource(*document),
                                activeStroke->ChangesView());
                        ++exactPreviewCompositionOrdinal_;
                        smartToolStrokePreviewPlanId_ = plan->PlanId();
                        smartToolStrokePreviewPlanRevision_ = plan->Revision();
                        smartToolStrokePreviewStrokeRevision_ =
                            activeStroke->Revision();
                    }
                    exactSmartToolPreview = &smartToolStrokePreviewMesh_;
                }
                else
                {
                    const std::size_t buildsBefore =
                        smartToolExactPreviewCache_.BuildCount();
                    exactSmartToolPreview = &smartToolExactPreviewCache_.Resolve(
                        ExactPreviewSource(*document),
                        voxelDocumentSession_.Generation(), plan);
                    // Le cache a-t-il réellement recomposé ? Sinon la géométrie
                    // est inchangée et le renderer ne doit rien réenvoyer.
                    if (smartToolExactPreviewCache_.BuildCount() != buildsBefore)
                        ++exactPreviewCompositionOrdinal_;
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
                // ERGO-01 LOT 0 : meme garde, sur le delta accumule du trait.
                activeStroke->ChangesView().size() <=
                    MaximumExactPreviewDeltaVoxelCount &&
                smartToolStrokePreviewPlan_ != nullptr &&
                smartToolStrokePreviewPlan_->BrushResult().RenderPlan.Mode ==
                    SmartBrushRenderMode::DetailedCells)
            {
                exactSmartToolPlan = smartToolStrokePreviewPlan_;
                if (smartToolStrokePreviewStrokeRevision_ !=
                    activeStroke->Revision())
                {
                    smartToolStrokePreviewMesh_ =
                        SmartToolExactPreviewComposer::Compose(
                            ExactPreviewSource(*document),
                            activeStroke->ChangesView());
                    ++exactPreviewCompositionOrdinal_;
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
                smartBrushPlacementPreview = &preview.Placement;
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
            // ERGO-01 LOT 2 : la boite presente l'etat final exact. L'expansion
            // vient de VoxelBoxService::CalculateChanges, extraite d'Apply pour
            // que preview et commit partagent une seule source de verite.
            //
            // MaximumExtentPerAxis vaut 64, donc une boite peut atteindre
            // 262 144 voxels, soit trente-deux fois le plafond de composition.
            // Le contour filaire est donc CONSERVE au-dela : c'est la
            // presentation de repli, et il ne faut pas repeter le trou du
            // LOT 1b.
            if (boxPreview)
            {
                const auto boxColor = paletteService_.ActiveColor();
                const std::uint8_t boxPalette = static_cast<std::uint8_t>(
                    boxColor ? boxColor->Index : 0U);
                std::uint64_t boxKey = 0x9E3779B97F4A7C15ULL;
                const auto mixBox = [&boxKey](const std::uint64_t value) noexcept
                {
                    boxKey ^= value;
                    boxKey *= 0xFF51AFD7ED558CCDULL;
                    boxKey ^= boxKey >> 29;
                };
                mixBox(3U); // discriminant d'outil : boite
                mixBox(document->GetRevision());
                mixBox(boxPalette);
                for (const std::int32_t component : {
                        boxPreview->Minimum.X, boxPreview->Minimum.Y,
                        boxPreview->Minimum.Z, boxPreview->Maximum.X,
                        boxPreview->Maximum.Y, boxPreview->Maximum.Z})
                {
                    mixBox(static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(component)));
                }
                const std::size_t boxVolume =
                    static_cast<std::size_t>(
                        boxPreview->Maximum.X - boxPreview->Minimum.X + 1) *
                    static_cast<std::size_t>(
                        boxPreview->Maximum.Y - boxPreview->Minimum.Y + 1) *
                    static_cast<std::size_t>(
                        boxPreview->Maximum.Z - boxPreview->Minimum.Z + 1);
                if (boxVolume <= MaximumExactPreviewDeltaVoxelCount)
                {
                    if (boxKey != geometricToolPreviewKey_)
                    {
                        const auto changes = VoxelBoxService::CalculateChanges(
                            *document, 0U, *boxPreview, boxPalette);
                        geometricToolPreviewMesh_ =
                            SmartToolExactPreviewComposer::Compose(
                                ExactPreviewSource(*document), changes);
                        ++exactPreviewCompositionOrdinal_;
                        geometricToolPreviewKey_ = boxKey;
                        ++geometricToolPreviewRevision_;
                    }
                    if (geometricToolPreviewMesh_.Succeeded())
                    {
                        exactSmartToolPreview = &geometricToolPreviewMesh_;
                        boxPreview.reset();
                    }
                }
            }
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
            // ERGO-01 LOT 2 : la ligne presente desormais l'ETAT FINAL EXACT,
            // opaque, comme le crayon et comme MagicaVoxel — au lieu du seul
            // contour filaire.
            //
            // La semantique du service est recopiee a l'identique : Apply SAUTE
            // les voxels deja occupes (VoxelLineService.cpp:147). Sans ce
            // filtre la preview annoncerait des poses qui n'auront pas lieu, ce
            // qui serait pire que l'ancien contour.
            // Meme source de couleur que l'Apply reel (EditorWorkspace.cpp:6483
            // et :6490) : sinon la preview annoncerait une autre couleur que
            // celle qui sera posee.
            const auto lineActiveColor = paletteService_.ActiveColor();
            const std::uint8_t linePalette = static_cast<std::uint8_t>(
                lineActiveColor ? lineActiveColor->Index : 0U);
            std::uint64_t lineKey = 0x9E3779B97F4A7C15ULL;
            const auto mixLine = [&lineKey](const std::uint64_t value) noexcept
            {
                lineKey ^= value;
                lineKey *= 0xFF51AFD7ED558CCDULL;
                lineKey ^= lineKey >> 29;
            };
            mixLine(1U); // discriminant d'outil : ligne
            mixLine(document->GetRevision());
            mixLine(linePalette);
            for (const Asset::Voxel::VoxelPosition& position : linePreview)
            {
                mixLine(static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(position.X)));
                mixLine(static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(position.Y)));
                mixLine(static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(position.Z)));
            }
            if (!linePreview.empty() &&
                linePreview.size() <= MaximumExactPreviewDeltaVoxelCount)
            {
                // Cle de contenu inchangee : on reutilise. Un survol immobile
                // ne recompose donc rien, comme pour le crayon.
                if (lineKey != geometricToolPreviewKey_)
                {
                    std::vector<Asset::Voxel::VoxelDocumentChange> changes;
                    changes.reserve(linePreview.size());
                    for (const Asset::Voxel::VoxelPosition& position :
                         linePreview)
                    {
                        if (document->HasVoxel(position, 0U)) continue;
                        changes.push_back({0U, position, false, 0U, true,
                            linePalette});
                    }
                    geometricToolPreviewMesh_ =
                        SmartToolExactPreviewComposer::Compose(
                            ExactPreviewSource(*document), changes);
                    ++exactPreviewCompositionOrdinal_;
                    geometricToolPreviewKey_ = lineKey;
                    ++geometricToolPreviewRevision_;
                }
                if (geometricToolPreviewMesh_.Succeeded())
                {
                    exactSmartToolPreview = &geometricToolPreviewMesh_;
                    // Le contour filaire ferait doublon avec l'etat final : la
                    // regle interdit la double presentation.
                    linePreview = {};
                }
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
            // ERGO-01 LOT 2 : la sphere presente l'etat final exact, comme la
            // ligne. Difference notable avec elle : le volume est CUBIQUE en
            // rayon, donc le plafond de delta se declenche pour de vrai — une
            // sphere de rayon 13 depasse deja 8 192 voxels. Dans ce cas on
            // CONSERVE le contour filaire des trois cercles : il ne faut pas
            // repeter le trou de presentation corrige au LOT 1b.
            const auto* const sphereDocument =
                voxelDocumentSession_.ActiveDocument();
            if (sphereDocument != nullptr)
            {
                std::vector<Asset::Voxel::VoxelPosition> spherePositions;
                try
                {
                    spherePositions = VoxelSphereService::CalculatePositions(
                        *sphereDocument, 0U,
                        *voxelSphereInteraction_.Center(),
                        *voxelSphereInteraction_.RadiusPoint());
                }
                catch (...)
                {
                    spherePositions.clear();
                }
                // Meme source de couleur que l'Apply reel, et meme filtre : le
                // service SAUTE les voxels deja occupes.
                const auto sphereColor = paletteService_.ActiveColor();
                const std::uint8_t spherePalette = static_cast<std::uint8_t>(
                    sphereColor ? sphereColor->Index : 0U);
                std::uint64_t sphereKey = 0x9E3779B97F4A7C15ULL;
                const auto mixSphere =
                    [&sphereKey](const std::uint64_t value) noexcept
                {
                    sphereKey ^= value;
                    sphereKey *= 0xFF51AFD7ED558CCDULL;
                    sphereKey ^= sphereKey >> 29;
                };
                mixSphere(2U); // discriminant d'outil : sphere
                mixSphere(sphereDocument->GetRevision());
                mixSphere(spherePalette);
                mixSphere(spherePositions.size());
                for (const Asset::Voxel::VoxelPosition& position :
                     spherePositions)
                {
                    mixSphere(static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(position.X)));
                    mixSphere(static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(position.Y)));
                    mixSphere(static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(position.Z)));
                }
                if (!spherePositions.empty() &&
                    spherePositions.size() <=
                        MaximumExactPreviewDeltaVoxelCount)
                {
                    if (sphereKey != geometricToolPreviewKey_)
                    {
                        std::vector<Asset::Voxel::VoxelDocumentChange> changes;
                        changes.reserve(spherePositions.size());
                        for (const Asset::Voxel::VoxelPosition& position :
                             spherePositions)
                        {
                            if (sphereDocument->HasVoxel(position, 0U))
                                continue;
                            changes.push_back({0U, position, false, 0U, true,
                                spherePalette});
                        }
                        geometricToolPreviewMesh_ =
                            SmartToolExactPreviewComposer::Compose(
                                ExactPreviewSource(*sphereDocument), changes);
                        ++exactPreviewCompositionOrdinal_;
                        geometricToolPreviewKey_ = sphereKey;
                        ++geometricToolPreviewRevision_;
                    }
                    if (geometricToolPreviewMesh_.Succeeded())
                    {
                        exactSmartToolPreview = &geometricToolPreviewMesh_;
                        spherePreview.reset();
                    }
                }
            }
        }
    }
    else
    {
        voxelPlacementPreview_ = {};
        const AddVoxelTarget addTarget = ResolveAddVoxelTarget();
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
            // ERGO-01 LOT 1b : combler le TROU DE PRESENTATION. En mode
            // DetailedCells, si le delta accumule depasse
            // MaximumExactPreviewDeltaVoxelCount, la preview exacte est
            // renoncee — et rien ne la remplacait : ni maillage exact, ni
            // fantomes (vides plus haut), ni agregat (pose seulement pour les
            // modes agreges). L'utilisateur dessinait donc a l'aveugle sur les
            // traits longs. On presente au moins l'enveloppe du plan, ce qui
            // respecte la regle « montrer ce qui va changer » a la precision
            // pres, au lieu de ne rien montrer du tout.
            else if (exactSmartToolPreview == nullptr &&
                // Face + Add pendant un trait presente DEJA des fantomes par
                // cellule (faceAddPlanGhostPresentation) : y ajouter l'enveloppe
                // agregee serait la double presentation que la regle interdit.
                !faceAddPlanGhostPresentation &&
                renderPlan.Bounds.Minimum.X <= renderPlan.Bounds.Maximum.X &&
                renderPlan.Bounds.Minimum.Y <= renderPlan.Bounds.Maximum.Y &&
                renderPlan.Bounds.Minimum.Z <= renderPlan.Bounds.Maximum.Z)
            {
                // SmartBrushBounds ne porte pas de drapeau de validite : on
                // verifie donc que la boite n'est pas vide composante par
                // composante, plutot que de supposer.
                brushAggregatePreview = VoxelBoxBounds{
                    renderPlan.Bounds.Minimum, renderPlan.Bounds.Maximum};
            }
        }
    }
    // ERGO-01 LOT 1a : cette suppression doit etre conditionnee au RENDU de la
    // preview exacte, pas seulement a son CALCUL. Sans le predicat
    // ShouldRenderExactPreviewGeometry, le cas « crayon un voxel hors trait »
    // calculait la preview, ne la rendait pas, et supprimait quand meme le
    // surlignage de survol : l'utilisateur perdait son repere sans rien gagner.
    // La condition est desormais exactement celle du bloc de presentation.
    if (ShouldRenderExactPreviewGeometry(
            previewSubject, smartToolStroke_.IsActive()) &&
        exactSmartToolPreview != nullptr && exactSmartToolPreview->Succeeded())
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
    // LOT 4a : sous-sondes du handoff. Aucun de ces appels ne touche le GPU,
    // et pourtant le handoff mesure 20-25 ms au survol rapide. On mesure au
    // lieu de supposer.
    {
    const EditorFrameProbeScope configureProbe(
        frameProbe_, EditorFrameProbeSlot::HoConfigure);
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
        smartBrushPlacementPreview,
        faceAddPlanGhostPresentation
            ? SmartBrushGhostGeometryStyle::ExposedFaceSurface
            : SmartBrushGhostGeometryStyle::VoxelBoxes,
        voxelModelCenter_);
    }
    const Asset::Voxel::VoxelDocument* const activeDocument =
        voxelDocumentSession_.ActiveDocument();
    {
    const EditorFrameProbeScope exactProbe(
        frameProbe_, EditorFrameProbeSlot::HoExact);
    // ERGO-01 LOT 2 : l'identite de presentation est generalisee. Elle venait
    // du SmartToolPlan, ce qui interdisait structurellement la preview exacte
    // aux outils geometriques — ils n'ont pas de plan. Elle vient desormais du
    // plan quand il existe, et de la cle de contenu de l'outil geometrique
    // sinon. Le predicat de rendu, lui, est inchange : pour ces outils
    // previewSubject vaut Geometric, et ShouldRenderExactPreviewGeometry
    // renvoie deja true.
    const std::uint64_t exactPreviewPlanId = exactSmartToolPlan != nullptr
        ? exactSmartToolPlan->PlanId()
        : geometricToolPreviewKey_;
    const std::uint64_t exactPreviewPlanRevision = exactSmartToolPlan != nullptr
        ? (smartToolStroke_.IsActive() ? smartToolStroke_.Revision()
                                       : exactSmartToolPlan->Revision())
        : geometricToolPreviewRevision_;
    if (ShouldRenderExactPreviewGeometry(
            previewSubject, smartToolStroke_.IsActive()) &&
        exactSmartToolPreview != nullptr &&
        exactSmartToolPreview->Succeeded())
    {
        // VF-0265 (lot 3g) : chemin chunké quand le compositeur a produit des
        // overrides. Le renderer refuse si le modèle n'est pas chunké — au
        // chargement, l'envoi est monolithique — et on retombe alors sur le
        // mesh assemblé. Jamais faux, seulement plus lent.
        bool presented = false;
        if (!exactSmartToolPreview->Overrides.empty())
        {
            std::vector<ViewportRenderer::ExactPreviewChunkUpdate> overrides;
            overrides.reserve(exactSmartToolPreview->Overrides.size());
            for (const SmartToolExactPreviewChunk& chunk :
                 exactSmartToolPreview->Overrides)
            {
                overrides.push_back({
                    ViewportRenderer::ModelChunkId{
                        chunk.Key.X, chunk.Key.Y, chunk.Key.Z},
                    &chunk.Mesh,
                    // LOT 4c : revision du contenu. Le renderer saute l'envoi
                    // GPU des chunks dont elle n'a pas bouge. Vaut zero quand le
                    // compositeur tourne sans cache, ce qui force l'envoi et
                    // preserve donc l'ancien comportement.
                    chunk.Revision});
            }
            presented = viewportRenderer_.ConfigureExactPreviewChunks(
                overrides, exactSmartToolPreview->Palette, voxelModelCenter_,
                exactSmartToolPreview->Active,
                voxelDocumentSession_.Generation(),
                activeDocument ? activeDocument->GetRevision() : 0U,
                exactPreviewCompositionOrdinal_);
        }
        if (!presented)
        {
            static_cast<void>(viewportRenderer_.ConfigureExactPreviewMesh(
                &exactSmartToolPreview->Mesh, &exactSmartToolPreview->Palette,
                voxelModelCenter_, exactSmartToolPreview->Active,
                voxelDocumentSession_.Generation(), activeDocument
                    ? activeDocument->GetRevision() : 0U,
                exactPreviewPlanId, exactPreviewPlanRevision));
        }
    }
    else if (!ShouldRetainExactPreviewOnMissingFrame(
                 previewSubject, smartToolStroke_.IsActive()))
    {
        static_cast<void>(viewportRenderer_.ConfigureExactPreviewMesh(
            nullptr, nullptr, {}, false, 0U, 0U, 0U, 0U));
    }
    }
    {
    const EditorFrameProbeScope voxelProbe(
        frameProbe_, EditorFrameProbeSlot::HoVoxel);
    viewportRenderer_.ConfigureVoxelPreview(
        stampPlacementSession_.CurrentPreview());
    }
    const EditorFrameProbeScope transformProbe(
        frameProbe_, EditorFrameProbeSlot::HoTransform);
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
