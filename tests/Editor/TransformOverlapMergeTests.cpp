// Transform Overlap / Merge (decision produit Tony) — Move, Scale, Rotate,
// Wrap : un recouvrement de geometrie existante n'est plus une invalidite.
//
// Pour chaque outil : preview autorisee (le recouvrement est SIGNALE), commit
// autorise, voxel transforme prioritaire sur la cellule commune, geometrie
// exterieure intacte, Undo restaure l'ancien voxel exactement, Redo restaure
// le resultat fusionne. Puis : plusieurs destinations vers une meme cellule
// (deduplication deterministe), recouvrement partiel, transformation
// entierement incluse dans une autre geometrie, et regles OutOfBounds
// inchangees. Mirror historique et Duplicate gardent leur blocage.

#include "Transform/MoveVoxelSelectionOperation.h"
#include "Transform/RotateVoxelSelectionOperation.h"
#include "Transform/ScaleVoxelSelectionOperation.h"
#include "Transform/WrapVoxelSelectionOperation.h"
#include "Transform/MirrorVoxelSelectionOperation.h"
#include "Transform/DuplicateVoxelSelectionOperation.h"
#include "Transform/TransformPreviewFeedback.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;
using VoxelValue = Asset::Voxel::Voxel;
using Cell = Asset::Vox::VoxVoxel;

constexpr std::uint64_t Generation = 81U;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

struct PositionLess final
{
    bool operator()(const Position a, const Position b) const noexcept
    {
        if (a.X != b.X) return a.X < b.X;
        if (a.Y != b.Y) return a.Y < b.Y;
        return a.Z < b.Z;
    }
};
using PaletteMap = std::map<Position, std::uint8_t, PositionLess>;

Asset::Voxel::VoxelDocument MakeDocument(const std::vector<Cell>& voxels)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {16U, 16U, 16U};
    model.Voxels = voxels;
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "transform-overlap-memory.vox");
    Require(loaded.Succeeded(), "Unable to build the overlap document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Overlap model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the overlap compatibility grid.");
    model->ForEachVoxel([&grid](const Position position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize the overlap compatibility grid.");
    });
    result.AddGrid(std::move(grid));
    return result;
}

class TestSession final : public VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibility(document)) {}
    std::uint64_t VoxelModelGeneration() const noexcept override { return Generation; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override { return CommandResult::Success(); }
    void CompleteVoxelEdit() noexcept override {}
    void UpdateVoxelEditSavedState(bool) noexcept override {}

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

SelectionService MakeSelection(const std::vector<Position>& positions)
{
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    static_cast<void>(selection.Apply(positions, SelectionMode::Replace));
    // Comme les tests Transform existants : les bornes editables suivent la
    // selection (le chemin par positions ne les pose pas).
    static_cast<void>(selection.ApplySortedVolume(
        selection.Voxels(), selection.Bounds(), SelectionMode::Replace));
    return selection;
}

PaletteMap Snapshot(const Asset::Voxel::VoxelDocument& document)
{
    PaletteMap result;
    document.GetModel(0U)->ForEachVoxel(
        [&result](const Position p, const VoxelValue v)
        {
            result[p] = v.PaletteIndex;
        });
    return result;
}

// Verifie le contrat commun a partir d'une operation prete : preview signale
// le recouvrement, commit -> `after`, Undo -> `before` exactement, Redo ->
// `after` exactement.
void CheckMergeContract(
    const char* const tool,
    Asset::Voxel::VoxelDocument& document,
    const TransformPreviewModel& preview,
    VoxelEditOperation operation,
    const PaletteMap& before,
    const PaletteMap& after)
{
    const std::string name(tool);
    Require(preview.HasCollisions(),
        name + ": the preview must report the overlap as information.");
    Require(Snapshot(document) == before,
        name + ": building the commit must not mutate the document.");
    TestSession session(document);
    VoxelEditHistory history;
    const VoxelEditHistoryResult executed =
        history.Execute(session, std::move(operation));
    Require(static_cast<bool>(executed), name + ": merge commit failed: " +
        executed.Message);
    Require(Snapshot(document) == after,
        name + ": merged document differs from the expected result.");
    Require(static_cast<bool>(history.Undo(session)) &&
        Snapshot(document) == before,
        name + ": Undo did not restore the previous voxels exactly.");
    Require(static_cast<bool>(history.Redo(session)) &&
        Snapshot(document) == after,
        name + ": Redo did not restore the merged result exactly.");
}

// --- Move ---------------------------------------------------------------------

void TestMoveOverlapMerges()
{
    // Source 3 voxels (3,4,5) en x = 1..3 ; existants : A = 9 en x = 6
    // (destination du voxel 5), et un voisin 12 en x = 7 hors destinations.
    auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U}, {6U, 1U, 1U, 9U}, {7U, 1U, 1U, 12U}});
    auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});
    const PaletteMap before = Snapshot(document);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation) &&
        preview.SetDelta(document, selection, Generation, {3, 0, 0}),
        "Move overlap preview could not be built.");
    MoveVoxelSelectionResult prepared = MoveVoxelSelectionOperation::Build(
        document, selection, Generation, preview);
    Require(prepared.Ready(), "Move overlap commit was refused: " + prepared.Message);
    PaletteMap after;
    after[{4, 1, 1}] = 3U; after[{5, 1, 1}] = 4U;
    after[{6, 1, 1}] = 5U;   // le voxel transforme (5) l'emporte sur A (9)
    after[{7, 1, 1}] = 12U;  // voisin intact
    CheckMergeContract("Move", document, preview,
        std::move(prepared.Operation), before, after);
}

// --- Scale --------------------------------------------------------------------

void TestScaleOverlapMerges()
{
    // Source 2 voxels (3,4) en x = 1..2 ; Scale X x2 -> x = 1..4 ; existant
    // A = 9 en x = 4 (recouvert par la copie de 4), voisin 12 en x = 5.
    auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {4U, 1U, 1U, 9U}, {5U, 1U, 1U, 12U}});
    auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}});
    const PaletteMap before = Snapshot(document);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Scale overlap preview could not begin.");
    const auto geometry = ScaleVoxelSelectionOperation::BuildGeometry(
        preview.SourceVoxels(), selection.EditableBounds(), VoxelScaleMode::X);
    Require(geometry.Valid() && preview.SetExplicitVoxelDestinations(
        document, selection, Generation, geometry.Destinations),
        "Scale overlap preview could not be built.");
    ScaleVoxelSelectionResult prepared = ScaleVoxelSelectionOperation::Build(
        document, selection, Generation, preview, VoxelScaleMode::X);
    Require(prepared.Ready(), "Scale overlap commit was refused: " + prepared.Message);
    PaletteMap after;
    after[{1, 1, 1}] = 3U; after[{2, 1, 1}] = 3U;
    after[{3, 1, 1}] = 4U; after[{4, 1, 1}] = 4U;   // 4 l'emporte sur A (9)
    after[{5, 1, 1}] = 12U;
    CheckMergeContract("Scale", document, preview,
        std::move(prepared.Operation), before, after);
}

// --- Rotate -------------------------------------------------------------------

void TestRotateOverlapMerges()
{
    // Meme configuration que le test Rotate historique : la rotation horaire
    // de {1,1,1},{2,1,1},{3,1,1},{1,1,2} a pour destination externe exacte
    // {2,1,3}, occupee par A = 9 ; un voisin 12 en {5,5,5} reste intact.
    const std::vector<Position> selected{{1, 1, 1}, {2, 1, 1}, {3, 1, 1}, {1, 1, 2}};
    auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {3U, 1U, 1U, 5U}, {1U, 1U, 2U, 6U}, {2U, 1U, 3U, 9U}, {5U, 5U, 5U, 12U}});
    auto selection = MakeSelection(selected);
    const PaletteMap before = Snapshot(document);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation),
        "Rotate overlap preview could not begin.");
    const auto geometry = RotateVoxelSelectionOperation::BuildGeometry(
        selection.Voxels(), selection.EditableBounds(),
        VoxelRotationDirection::Clockwise);
    Require(geometry.Valid() && preview.SetExplicitDestinations(
        document, selection, Generation, geometry.Destinations),
        "Rotate overlap preview could not be built.");
    Require(preview.CollisionPositions().size() == 1U &&
        preview.CollisionPositions().front() == Position{2, 1, 3},
        "Rotate overlap preview did not report the exact external overlap.");
    RotateVoxelSelectionResult prepared = RotateVoxelSelectionOperation::Build(
        document, selection, Generation, preview,
        VoxelRotationDirection::Clockwise);
    Require(prepared.Ready(), "Rotate overlap commit was refused: " + prepared.Message);
    // Resultat attendu : les destinations de la geometrie portent les valeurs
    // source ; A (9) est remplace en {2,1,3} ; {5,5,5} intact.
    // Les destinations suivent l'ordre TRIE de la selection (SelectionService).
    PaletteMap after;
    const auto sorted = selection.Voxels();
    for (std::size_t index = 0U; index < sorted.size(); ++index)
    {
        const auto value = document.GetVoxel(sorted[index]);
        after[geometry.Destinations[index]] = value->PaletteIndex;
    }
    after[{5, 5, 5}] = 12U;
    Require(after.count({2, 1, 3}) == 1U && after.at({2, 1, 3}) != 9U,
        "Rotate expected result does not overwrite the overlapped cell.");
    CheckMergeContract("Rotate", document, preview,
        std::move(prepared.Operation), before, after);
}

// --- Wrap ---------------------------------------------------------------------

void TestWrapOverlapMerges()
{
    // Motif AB (3|11) en x = 2..3, cible {2..7} ; existants A = 9 en x = 5
    // (destination du B repete) et voisin 12 en {2,5,5}.
    auto document = MakeDocument({{2U, 1U, 1U, 3U}, {3U, 1U, 1U, 11U},
        {5U, 1U, 1U, 9U}, {2U, 5U, 5U, 12U}});
    const SelectionBounds source = SelectionBounds::FromCorners({2, 1, 1}, {3, 1, 1});
    const SelectionBounds target = SelectionBounds::FromCorners({2, 1, 1}, {7, 1, 1});
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    const std::vector<Position> selected{{2, 1, 1}, {3, 1, 1}};
    Require(selection.ApplySortedVolume(selected, source, SelectionMode::Replace),
        "Unable to select the Wrap overlap pattern.");
    const PaletteMap before = Snapshot(document);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Wrap overlap preview could not begin.");
    const WrapPatternIndex pattern(preview.SourceVoxels(), source);
    const VoxelWrapGeometry geometry =
        WrapVoxelSelectionOperation::BuildGeometry(pattern, target, {});
    Require(geometry.Valid() && preview.SetExplicitVoxelDestinations(
        document, selection, Generation, geometry.Destinations),
        "Wrap overlap preview could not be built.");
    WrapVoxelSelectionResult prepared = WrapVoxelSelectionOperation::Build(
        document, selection, Generation, preview, source, target, {});
    Require(prepared.Ready(), "Wrap overlap commit was refused: " + prepared.Message);
    PaletteMap after;
    for (std::int32_t x = 2; x <= 7; ++x)
        after[{x, 1, 1}] = x % 2 == 0 ? 3U : 11U;   // 5 -> 11 l'emporte sur 9
    after[{2, 5, 5}] = 12U;
    CheckMergeContract("Wrap", document, preview,
        std::move(prepared.Operation), before, after);
}

// --- Cas transverses ------------------------------------------------------------

void TestMultipleDestinationsOnOneCellAreDeduplicated()
{
    // Deux sources transformees vers la MEME cellule (destinations explicites)
    // sur une cellule deja occupee : une seule destination finale, la derniere
    // emise l'emporte, deterministe ; Undo restaure l'existant.
    auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {6U, 1U, 1U, 9U}});
    auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}});
    const PaletteMap before = Snapshot(document);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Dedup preview could not begin.");
    const std::vector<TransformPreviewDestinationVoxel> destinations{
        {{1, 1, 1}, {6, 1, 1}, VoxelValue{3U}},
        {{2, 1, 1}, {6, 1, 1}, VoxelValue{4U}}};
    Require(preview.SetExplicitVoxelDestinations(
        document, selection, Generation, destinations),
        "Dedup preview refused two destinations on one cell.");
    TransformOperationRequest request;
    request.Name = "Merge"; request.Label = "Merge";
    request.Policy = {TransformSourcePolicy::RemoveSource,
        TransformCollisionPolicy::MergeOverlap};
    request.DestinationBounds = SelectionBounds::FromCorners({6, 1, 1}, {6, 1, 1});
    TransformOperationBuildResult built = TransformOperationBuilder::Build(
        document, selection, Generation, preview, request);
    Require(built.Ready(), "Dedup merge commit was refused: " + built.Message);
    Require(built.Operation.SelectionTransition->After.Voxels ==
            std::vector<Position>{{6, 1, 1}},
        "Dedup did not leave exactly one destination.");
    // Les politiques historiques refusent toujours les doublons (verifie
    // sur le document encore intact).
    TransformPreviewModel strict;
    Require(strict.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource) &&
        strict.SetExplicitVoxelDestinations(
            document, selection, Generation, destinations),
        "Strict dedup preview could not be built.");
    TransformOperationRequest strictRequest = request;
    strictRequest.Policy.Collision = TransformCollisionPolicy::AllowSourceOverlap;
    Require(!TransformOperationBuilder::Build(
        document, selection, Generation, strict, strictRequest).Ready(),
        "AllowSourceOverlap must still refuse duplicate destinations.");
    PaletteMap after;
    after[{6, 1, 1}] = 4U;   // la derniere destination emise (source 2 -> 4)
    CheckMergeContract("Dedup", document, preview,
        std::move(built.Operation), before, after);
}

void TestPartialAndFullyIncludedOverlaps()
{
    // Partiel : Move de 3 voxels sur une rangee de 2 existants (2 recouverts,
    // 1 libre) ; les existants voisins restent.
    {
        auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
            {3U, 1U, 1U, 5U}, {5U, 1U, 1U, 20U}, {6U, 1U, 1U, 21U},
            {8U, 1U, 1U, 22U}});
        auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}, {3, 1, 1}});
        const PaletteMap before = Snapshot(document);
        TransformPreviewModel preview;
        Require(preview.BeginPreview(document, selection, Generation) &&
            preview.SetDelta(document, selection, Generation, {3, 0, 0}),
            "Partial overlap preview could not be built.");
        Require(preview.CollisionCount() == 2U,
            "Partial overlap must report exactly the two covered cells.");
        MoveVoxelSelectionResult prepared = MoveVoxelSelectionOperation::Build(
            document, selection, Generation, preview);
        Require(prepared.Ready(), "Partial overlap commit was refused.");
        PaletteMap after;
        after[{4, 1, 1}] = 3U; after[{5, 1, 1}] = 4U; after[{6, 1, 1}] = 5U;
        after[{8, 1, 1}] = 22U;
        CheckMergeContract("Partial", document, preview,
            std::move(prepared.Operation), before, after);
    }
    // Entierement inclus : Move d'un voxel a l'interieur d'un bloc plein 3x3x3
    // -> le bloc garde toutes ses cellules, celle recouverte prend la valeur
    // transformee ; la cellule source liberee devient vide.
    {
        std::vector<Cell> cells{{0U, 0U, 0U, 7U}};
        for (std::uint8_t z = 4U; z < 7U; ++z)
            for (std::uint8_t y = 4U; y < 7U; ++y)
                for (std::uint8_t x = 4U; x < 7U; ++x)
                    cells.push_back({x, y, z, 30U});
        auto document = MakeDocument(cells);
        auto selection = MakeSelection({{0, 0, 0}});
        const PaletteMap before = Snapshot(document);
        TransformPreviewModel preview;
        Require(preview.BeginPreview(document, selection, Generation) &&
            preview.SetDelta(document, selection, Generation, {5, 5, 5}),
            "Included overlap preview could not be built.");
        MoveVoxelSelectionResult prepared = MoveVoxelSelectionOperation::Build(
            document, selection, Generation, preview);
        Require(prepared.Ready(), "Included overlap commit was refused.");
        PaletteMap after = before;
        after.erase({0, 0, 0});
        after[{5, 5, 5}] = 7U;
        CheckMergeContract("Included", document, preview,
            std::move(prepared.Operation), before, after);
    }
}

void TestOutOfBoundsAndOtherPoliciesUnchanged()
{
    // Hors limites : toujours refuse, meme sous MergeOverlap.
    auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {6U, 1U, 1U, 9U}});
    auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}});
    TransformPreviewModel out;
    Require(out.BeginPreview(document, selection, Generation) &&
        out.SetDelta(document, selection, Generation, {-2, 0, 0}) &&
        out.HasOutOfBounds(),
        "Out-of-bounds preview could not be built.");
    Require(MoveVoxelSelectionOperation::Build(
        document, selection, Generation, out).Code ==
            MoveVoxelSelectionResultCode::OutOfBounds,
        "Out of bounds must still be refused under MergeOverlap.");
    // Mirror historique (AllowSourceOverlap) et Duplicate (RejectAny)
    // gardent leur blocage sur un recouvrement externe.
    TransformPreviewModel duplicate;
    Require(duplicate.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IncludeSource) &&
        duplicate.SetDelta(document, selection, Generation, {4, 0, 0}),
        "Duplicate overlap preview could not be built.");
    Require(!DuplicateVoxelSelectionOperation::Build(
        document, selection, Generation, duplicate).Ready(),
        "Duplicate must still refuse an occupied destination.");
}
// --- Feedback commun : hors-limites > overlap > normal --------------------------

void TestFeedbackPriorityHelper()
{
    // Autorite pure, testee directement (les cinq outils la consomment).
    Require(ResolveTransformPreviewFeedback(false, false) ==
            TransformPreviewFeedback::Normal,
        "No overlap, in bounds: normal feedback expected.");
    Require(ResolveTransformPreviewFeedback(false, true) ==
            TransformPreviewFeedback::Overlap &&
        !TransformPreviewFeedbackBlocksCommit(TransformPreviewFeedback::Overlap),
        "Overlap alone must be informative and never block the commit.");
    Require(ResolveTransformPreviewFeedback(true, false) ==
            TransformPreviewFeedback::OutOfBounds &&
        TransformPreviewFeedbackBlocksCommit(TransformPreviewFeedback::OutOfBounds),
        "Out of bounds alone must block the commit.");
    Require(ResolveTransformPreviewFeedback(true, true) ==
            TransformPreviewFeedback::OutOfBounds,
        "Out of bounds must win over overlap: never announce a merge that "
        "the commit will refuse.");
}

void TestOverlapPlusOutOfBoundsIsBlockedAndReported()
{
    // Cas combine (document 16^3) : selection {1,1,1},{2,1,1},{14,1,1},
    // delta {2,0,0} -> 3 (libre), 4 (occupe par A = 9 : overlap), 16 (hors
    // du modele : bloquant). Une partie recouvre, une partie sort.
    auto document = MakeDocument({{1U, 1U, 1U, 3U}, {2U, 1U, 1U, 4U},
        {14U, 1U, 1U, 5U}, {4U, 1U, 1U, 9U}, {8U, 8U, 8U, 12U}});
    auto selection = MakeSelection({{1, 1, 1}, {2, 1, 1}, {14, 1, 1}});
    const PaletteMap before = Snapshot(document);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation) &&
        preview.SetDelta(document, selection, Generation, {2, 0, 0}),
        "Combined preview could not be built.");
    // Les deux sont detectes : l'overlap (information) et le hors-limites.
    Require(preview.HasCollisions() && preview.CollisionCount() == 1U &&
        preview.HasOutOfBounds() && preview.OutOfBoundsCount() == 1U,
        "Combined preview must report both the overlap and the out of bounds.");
    // Verdict commun : hors-limites gagne, le commit est bloque.
    Require(ResolveTransformPreviewFeedback(preview) ==
            TransformPreviewFeedback::OutOfBounds &&
        !MergingTransformPreviewCanCommit(preview),
        "Combined feedback must be OutOfBounds and the commit must be blocked.");
    // Et le framework refuse bien pour hors-limites, jamais pour l'overlap.
    const MoveVoxelSelectionResult refused = MoveVoxelSelectionOperation::Build(
        document, selection, Generation, preview);
    Require(refused.Code == MoveVoxelSelectionResultCode::OutOfBounds &&
        Snapshot(document) == before,
        "Combined commit must be refused as OutOfBounds without mutation.");
    // Contre-epreuves sur le meme document : overlap seul -> informatif et
    // committable ; hors-limites seul -> bloquant.
    TransformPreviewModel overlapOnly;
    auto twoInside = MakeSelection({{1, 1, 1}, {2, 1, 1}});
    Require(overlapOnly.BeginPreview(document, twoInside, Generation) &&
        overlapOnly.SetDelta(document, twoInside, Generation, {2, 0, 0}) &&
        overlapOnly.HasCollisions() && !overlapOnly.HasOutOfBounds() &&
        ResolveTransformPreviewFeedback(overlapOnly) ==
            TransformPreviewFeedback::Overlap &&
        MergingTransformPreviewCanCommit(overlapOnly) &&
        MoveVoxelSelectionOperation::Build(
            document, twoInside, Generation, overlapOnly).Ready(),
        "Overlap alone must be informative and committable.");
    TransformPreviewModel outOnly;
    auto edge = MakeSelection({{14, 1, 1}});
    Require(outOnly.BeginPreview(document, edge, Generation) &&
        outOnly.SetDelta(document, edge, Generation, {2, 0, 0}) &&
        !outOnly.HasCollisions() && outOnly.HasOutOfBounds() &&
        ResolveTransformPreviewFeedback(outOnly) ==
            TransformPreviewFeedback::OutOfBounds &&
        !MergingTransformPreviewCanCommit(outOnly) &&
        MoveVoxelSelectionOperation::Build(
            document, edge, Generation, outOnly).Code ==
            MoveVoxelSelectionResultCode::OutOfBounds,
        "Out of bounds alone must block.");
}
} // namespace

int main()
{
    try
    {
        TestMoveOverlapMerges();
        TestScaleOverlapMerges();
        TestRotateOverlapMerges();
        TestWrapOverlapMerges();
        TestMultipleDestinationsOnOneCellAreDeduplicated();
        TestPartialAndFullyIncludedOverlaps();
        TestOutOfBoundsAndOtherPoliciesUnchanged();
        TestFeedbackPriorityHelper();
        TestOverlapPlusOutOfBoundsIsBlockedAndReported();
        std::cout << "Transform overlap/merge tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Transform overlap/merge tests failed: " << exception.what()
            << '\n';
        return 1;
    }
}
