// VF-WRAP-V1 (correctif) — Wrap au niveau document / historique.
//
// Ces tests couvrent ce que la geometrie pure ne peut pas : le partage des
// bornes source entre preview et commit (bord vide), la transition
// d'historique portant les EditableBounds semantiques (Undo -> source, Redo
// -> demandees), l'equivalence Wrap A -> Undo -> Redo -> Wrap B == Wrap A ->
// Wrap B, l'accord resume/geometrie et suivi de collisions/modele exact
// (mode compact), et l'extension generique du framework (bornes editables
// optionnelles : comportement historique inchange quand elles sont absentes).

#include "Transform/WrapVoxelSelectionOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
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

constexpr std::uint64_t Generation = 71U;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

using Cell = Asset::Vox::VoxVoxel;

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

Asset::Voxel::VoxelDocument MakeDocument(
    const std::vector<Cell>& voxels,
    const std::uint32_t dimension = 32U)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {dimension, dimension, dimension};
    model.Voxels = voxels;
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "wrap-commit-memory.vox");
    Require(loaded.Succeeded(), "Unable to build the Wrap commit document.");
    return std::move(*loaded.Document);
}

Voxel::VoxelModel MakeCompatibility(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Wrap commit model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the Wrap compatibility grid.");
    model->ForEachVoxel([&grid](const Position position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize the Wrap compatibility grid.");
    });
    result.AddGrid(std::move(grid));
    return result;
}

class TestSession final : public VoxelEditSession
{
public:
    explicit TestSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibility(document)) {}
    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return Generation;
    }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override {}
    void UpdateVoxelEditSavedState(bool) noexcept override {}

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
};

// Le workspace applique la transition d'historique a la selection : la meme
// regle ici (Before apres Undo, After apres commit et Redo).
void ApplyTransition(SelectionService& selection,
    const VoxelEditHistoryResult& result)
{
    Require(result.SelectionTransition != nullptr,
        "Wrap history lost its selection transition.");
    const auto& snapshot = result.SelectionState ==
        VoxelEditSelectionState::Before
        ? result.SelectionTransition->Before
        : result.SelectionTransition->After;
    selection.SetDocumentGeneration(snapshot.DocumentGeneration);
    static_cast<void>(selection.ApplySortedVolume(
        snapshot.Voxels, snapshot.Bounds, SelectionMode::Replace));
}

PaletteMap Snapshot(
    const Asset::Voxel::VoxelDocument& document)
{
    PaletteMap result;
    document.GetModel(0U)->ForEachVoxel(
        [&result](const Position p, const VoxelValue v)
        {
            result[p] = v.PaletteIndex;
        });
    return result;
}

PaletteMap Snapshot(
    const std::vector<TransformPreviewDestinationVoxel>& destinations)
{
    PaletteMap result;
    for (const auto& d : destinations)
        result[d.DestinationPosition] = d.Value.PaletteIndex;
    return result;
}

// Une "session Wrap" minimale : preview -> commit par le VRAI chemin
// operation + historique, comme le workspace, mais pilotee par des bornes.
struct WrapDriver final
{
    Asset::Voxel::VoxelDocument& Document;
    SelectionService& Selection;
    VoxelEditHistory& History;
    TestSession& Session;
    TransformPreviewModel Preview;

    // Retourne la geometrie de preview (materialisee) et le resultat commit.
    std::pair<VoxelWrapGeometry, WrapVoxelSelectionResult> Wrap(
        const SelectionBounds sourceBounds, const SelectionBounds target,
        const VoxelWrapOptions& options)
    {
        Require(Preview.BeginPreview(Document, Selection, Generation, 0U,
            TransformPreviewCollisionPolicy::IgnoreSource),
            "Wrap driver could not begin the preview.");
        const WrapPatternIndex pattern(Preview.SourceVoxels(), sourceBounds);
        VoxelWrapGeometry preview = WrapVoxelSelectionOperation::BuildGeometry(
            pattern, target, options);
        Require(preview.Valid(), "Wrap driver preview geometry is invalid.");
        Require(Preview.SetExplicitVoxelDestinations(
            Document, Selection, Generation, preview.Destinations),
            "Wrap driver could not update the preview.");
        WrapVoxelSelectionResult commit = WrapVoxelSelectionOperation::Build(
            Document, Selection, Generation, Preview, sourceBounds, target,
            options);
        static_cast<void>(Preview.CancelPreview());
        return {std::move(preview), std::move(commit)};
    }

    VoxelEditHistoryResult Execute(WrapVoxelSelectionResult& commit)
    {
        Require(commit.Ready(), "Wrap driver commit is not ready: " +
            commit.Message);
        const VoxelEditHistoryResult result = History.Execute(
            Session, std::move(commit.Operation));
        Require(static_cast<bool>(result), "Wrap driver execute failed.");
        ApplyTransition(Selection, result);
        return result;
    }
};

// --- 1. Bord vide : preview et commit partagent les EditableBounds source ---

void TestEmptyEdgeSourceBoundsPreviewEqualsCommit()
{
    // Un seul voxel occupe en X = 2, EditableBounds source {2..3} : le motif
    // est |X.| (voxel + vide). Cible {2..5} : ABAB -> X . X . -> voxels en
    // 2 et 4 seulement. Avec les bornes SERREES ({2..2}) on aurait 2,3,4,5 —
    // c'est exactement la divergence preview/commit corrigee.
    auto document = MakeDocument({{2U, 1U, 1U, 7U}});
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    const SelectionBounds source = SelectionBounds::FromCorners({2, 1, 1}, {3, 1, 1});
    const SelectionBounds target = SelectionBounds::FromCorners({2, 1, 1}, {5, 1, 1});
    const std::vector<Position> selected{{2, 1, 1}};
    Require(selection.ApplySortedVolume(selected, source, SelectionMode::Replace),
        "Unable to select the empty-edge pattern.");
    Require(selection.EditableBounds() == source,
        "Empty-edge selection did not keep its editable bounds.");
    VoxelEditHistory history;
    TestSession session(document);
    WrapDriver driver{document, selection, history, session, {}};

    auto [preview, commit] = driver.Wrap(source, target, {});
    Require(preview.Destinations.size() == 2U &&
        Snapshot(preview.Destinations) ==
            PaletteMap{{{2, 1, 1}, 7U}, {{4, 1, 1}, 7U}},
        "Empty-edge preview must repeat the void with the voxel.");
    Require(commit.Ready(), "Empty-edge commit was refused: " + commit.Message);
    // La transition porte les bornes editables SEMANTIQUES, pas les serrees.
    Require(commit.Operation.SelectionTransition->Before.Bounds == source &&
        commit.Operation.SelectionTransition->After.Bounds == target,
        "Empty-edge transition does not carry the editable bounds.");
    Require(commit.Operation.SelectionTransition->After.Voxels ==
            std::vector<Position>{{2, 1, 1}, {4, 1, 1}},
        "Empty-edge commit selection differs from the preview.");
    driver.Execute(commit);
    Require(Snapshot(document) == Snapshot(preview.Destinations),
        "PREVIEW == COMMIT violated on the empty-edge pattern.");
    Require(selection.EditableBounds() == target &&
        selection.Count() == 2U,
        "Empty-edge commit did not leave the requested editable bounds.");
    // Une cible dont la derniere cellule est vide (2..5 -> occupe 2,4) garde
    // les bornes DEMANDEES {2..5}, pas les serrees {2..4}.
    Require(selection.EditableBounds().Maximum.X == 5,
        "Requested bounds with an empty last cell were tightened.");
}

// --- 2. Historique : Undo -> source, Redo -> demandees ; A/Undo/Redo/B ---

struct WrapOutcome final
{
    PaletteMap Document;
    std::vector<Position> Selection;
    SelectionBounds EditableBounds{};
    [[nodiscard]] bool operator==(const WrapOutcome&) const noexcept = default;
};

WrapOutcome RunSequence(const bool undoRedoBetween)
{
    // Motif AB en X = 2..3 (EditableBounds {2..3}) sur une ligne.
    auto document = MakeDocument({{2U, 1U, 1U, 3U}, {3U, 1U, 1U, 11U}});
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    const SelectionBounds sourceA = SelectionBounds::FromCorners({2, 1, 1}, {3, 1, 1});
    const std::vector<Position> selected{{2, 1, 1}, {3, 1, 1}};
    Require(selection.ApplySortedVolume(selected, sourceA, SelectionMode::Replace),
        "Unable to select the AB pattern.");
    VoxelEditHistory history;
    TestSession session(document);
    WrapDriver driver{document, selection, history, session, {}};

    // Wrap A : X+ jusqu'a 7 (ABABAB), spacing 0.
    const SelectionBounds targetA = SelectionBounds::FromCorners({2, 1, 1}, {7, 1, 1});
    auto [previewA, commitA] = driver.Wrap(sourceA, targetA, {});
    driver.Execute(commitA);
    Require(selection.EditableBounds() == targetA && selection.Count() == 6U,
        "Wrap A did not leave the requested bounds.");

    if (undoRedoBetween)
    {
        const VoxelEditHistoryResult undone = history.Undo(session);
        Require(static_cast<bool>(undone), "Undo of Wrap A failed.");
        ApplyTransition(selection, undone);
        Require(selection.EditableBounds() == sourceA &&
            selection.Count() == 2U && Snapshot(document).size() == 2U,
            "Undo did not restore voxels + source EditableBounds exactly.");
        const VoxelEditHistoryResult redone = history.Redo(session);
        Require(static_cast<bool>(redone), "Redo of Wrap A failed.");
        ApplyTransition(selection, redone);
        Require(selection.EditableBounds() == targetA &&
            selection.Count() == 6U && Snapshot(document).size() == 6U,
            "Redo did not restore voxels + requested EditableBounds exactly.");
    }

    // Wrap B depuis l'etat courant : source = EditableBounds courantes
    // ({2..7}), cible {2..13} avec spacing 1 et mirror X.
    const SelectionBounds sourceB = selection.EditableBounds();
    VoxelWrapOptions optionsB;
    optionsB.X.Spacing = 1;
    optionsB.X.MirrorRepeat = true;
    const SelectionBounds targetB = SelectionBounds::FromCorners({2, 1, 1}, {13, 1, 1});
    auto [previewB, commitB] = driver.Wrap(sourceB, targetB, optionsB);
    driver.Execute(commitB);
    Require(Snapshot(document) == Snapshot(previewB.Destinations),
        "PREVIEW == COMMIT violated on Wrap B.");
    return {Snapshot(document),
        std::vector<Position>(selection.Voxels().begin(),
            selection.Voxels().end()),
        selection.EditableBounds()};
}

void TestUndoRedoBetweenWrapsIsTransparent()
{
    const WrapOutcome direct = RunSequence(false);
    const WrapOutcome viaHistory = RunSequence(true);
    Require(direct == viaHistory,
        "Wrap A -> Undo -> Redo -> Wrap B differs from Wrap A -> Wrap B.");
    Require(direct.EditableBounds == SelectionBounds::FromCorners({2, 1, 1}, {13, 1, 1}),
        "Wrap B did not leave its requested bounds.");
    // ABABAB (2..7), spacing 1 (8 vide), miroir BABABA (9..14 -> tronque a 13).
    Require(direct.Document.count({8, 1, 1}) == 0U &&
        direct.Document.at({9, 1, 1}) == 11U && direct.Document.at({10, 1, 1}) == 3U &&
        direct.Document.at({13, 1, 1}) == 11U,
        "Wrap B did not produce the mirrored, spaced repetition.");
}

// --- 3. Framework : bornes editables optionnelles ---------------------------

void TestFrameworkEditableBoundsAreOptionalAndValidated()
{
    auto document = MakeDocument({{2U, 1U, 1U, 3U}, {3U, 1U, 1U, 11U}});
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    const std::vector<Position> selected{{2, 1, 1}, {3, 1, 1}};
    const SelectionBounds editable = SelectionBounds::FromCorners({2, 1, 1}, {5, 1, 1});
    Require(selection.ApplySortedVolume(selected, editable, SelectionMode::Replace),
        "Unable to select the framework pattern.");
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Framework preview could not begin.");
    Require(preview.SetDelta(document, selection, Generation, {0, 0, 4}),
        "Framework preview could not move.");

    // Sans bornes editables : Before/After = bornes serrees (historique).
    TransformOperationRequest plain;
    plain.Name = "Test"; plain.Label = "Test";
    plain.DestinationBounds = preview.PreviewBounds();
    const auto plainResult = TransformOperationBuilder::Build(
        document, selection, Generation, preview, plain);
    Require(plainResult.Ready(), "Plain framework build was refused.");
    Require(plainResult.Operation.SelectionTransition->Before.Bounds ==
            SelectionBounds::FromCorners({2, 1, 1}, {3, 1, 1}) &&
        plainResult.Operation.SelectionTransition->After.Bounds ==
            SelectionBounds::FromCorners({2, 1, 5}, {3, 1, 5}),
        "Without editable bounds the transition must keep tight bounds.");

    // Avec bornes editables valides : Before/After les portent.
    TransformOperationRequest semantic = plain;
    semantic.SourceEditableBounds = editable;
    semantic.DestinationEditableBounds =
        SelectionBounds::FromCorners({2, 1, 5}, {5, 1, 5});
    const auto semanticResult = TransformOperationBuilder::Build(
        document, selection, Generation, preview, semantic);
    Require(semanticResult.Ready(), "Semantic framework build was refused.");
    Require(semanticResult.Operation.SelectionTransition->Before.Bounds ==
            editable &&
        semanticResult.Operation.SelectionTransition->After.Bounds ==
            semantic.DestinationEditableBounds,
        "Editable bounds were not carried by the transition.");

    // Bornes editables qui ne contiennent pas le resultat : refusees.
    TransformOperationRequest wrong = plain;
    wrong.DestinationEditableBounds =
        SelectionBounds::FromCorners({2, 1, 5}, {2, 1, 5});
    Require(!TransformOperationBuilder::Build(
        document, selection, Generation, preview, wrong).Ready(),
        "Editable bounds not containing the result were accepted.");
    TransformOperationRequest wrongSource = plain;
    wrongSource.SourceEditableBounds =
        SelectionBounds::FromCorners({3, 1, 1}, {3, 1, 1});
    Require(!TransformOperationBuilder::Build(
        document, selection, Generation, preview, wrongSource).Ready(),
        "Source editable bounds not containing the selection were accepted.");
}

// --- 4. Mode compact : resume == geometrie, collisions == modele exact -----

void TestSummaryMatchesMaterialisedGeometry()
{
    std::mt19937 rng(20260815U);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> extent(1, 4);
    std::uniform_int_distribution<int> spacing(0, 2);
    std::uniform_int_distribution<int> grow(0, 9);
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        // Motif aleatoire dans des bornes source aleatoires (bord vide
        // possible), options aleatoires, cible aleatoire (crop ou repeat).
        const Position sourceMin{extent(rng), extent(rng), extent(rng)};
        const Position sourceMax{
            sourceMin.X + extent(rng) - 1, sourceMin.Y + extent(rng) - 1,
            sourceMin.Z + extent(rng) - 1};
        const SelectionBounds source = SelectionBounds::FromCorners(sourceMin, sourceMax);
        std::vector<TransformPreviewVoxel> voxels;
        for (std::int32_t x = sourceMin.X; x <= sourceMax.X; ++x)
            for (std::int32_t y = sourceMin.Y; y <= sourceMax.Y; ++y)
                for (std::int32_t z = sourceMin.Z; z <= sourceMax.Z; ++z)
                    if (coin(rng))
                        voxels.push_back({{x, y, z}, {x, y, z},
                            VoxelValue{static_cast<std::uint8_t>(1 + (x + y + z) % 5)},
                            TransformPreviewVoxelState::Valid});
        if (voxels.empty()) continue;
        VoxelWrapOptions options;
        for (WrapAxisOptions* axis : {&options.X, &options.Y, &options.Z})
        {
            axis->Spacing = spacing(rng);
            axis->MirrorRepeat = coin(rng) == 1;
            axis->Anchor = coin(rng) ? WrapAxisAnchor::Minimum
                                     : WrapAxisAnchor::Maximum;
        }
        const Position targetMin{
            sourceMin.X - (coin(rng) ? grow(rng) : 0),
            sourceMin.Y - (coin(rng) ? grow(rng) : 0),
            sourceMin.Z - (coin(rng) ? grow(rng) : 0)};
        const Position targetMax{
            std::max(targetMin.X, sourceMax.X + grow(rng) - 3),
            std::max(targetMin.Y, sourceMax.Y + grow(rng) - 3),
            std::max(targetMin.Z, sourceMax.Z + grow(rng) - 3)};
        const SelectionBounds target = SelectionBounds::FromCorners(targetMin, targetMax);
        const WrapPatternIndex pattern(voxels, source);
        const VoxelWrapSummary summary =
            WrapVoxelSelectionOperation::Summarize(pattern, target, options);
        const VoxelWrapGeometry geometry =
            WrapVoxelSelectionOperation::BuildGeometry(pattern, target, options);
        Require(summary.Valid() == geometry.Valid(),
            "Summary and geometry disagree on validity.");
        if (!summary.Valid()) continue;
        Require(summary.DestinationCount == geometry.Destinations.size(),
            "Summary count differs from the materialised geometry.");
        Require(summary.Bounds == geometry.Bounds,
            "Summary bounds differ from the materialised geometry.");
        // Aucune destination dupliquee : le framework les refuserait.
        const auto snapshot = Snapshot(geometry.Destinations);
        Require(snapshot.size() == geometry.Destinations.size(),
            "Materialised geometry produced duplicate destinations.");
    }
}

void TestCollisionTrackerMatchesExactModel()
{
    // Motif 2x2x2 a l'origine ; obstacles : un mur 8 x 12 x 12 en x = 10..17
    // (1 152 voxels) et un obstacle isole. Le suivi incremental (croissance,
    // retrait, changement d'options, saut) doit donner a chaque etape le
    // meme compte que le modele exact.
    std::vector<Cell> cells;
    for (std::uint8_t z = 0U; z < 2U; ++z)
        for (std::uint8_t y = 0U; y < 2U; ++y)
            for (std::uint8_t x = 0U; x < 2U; ++x)
                cells.push_back({x, y, z, static_cast<std::uint8_t>(1U + x)});
    for (std::uint8_t z = 0U; z < 12U; ++z)
        for (std::uint8_t y = 0U; y < 12U; ++y)
            for (std::uint8_t x = 10U; x < 18U; ++x)
                cells.push_back({x, y, z, 12U});
    cells.push_back({25U, 3U, 3U, 9U});
    auto document = MakeDocument(cells);
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    std::vector<Position> selected;
    for (std::int32_t x = 0; x < 2; ++x)
        for (std::int32_t y = 0; y < 2; ++y)
            for (std::int32_t z = 0; z < 2; ++z)
                selected.push_back({x, y, z});
    const SelectionBounds source = SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});
    Require(selection.ApplySortedVolume(selected, source, SelectionMode::Replace),
        "Unable to select the tracker pattern.");
    TransformPreviewModel exact;
    Require(exact.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Tracker exact preview could not begin.");
    const WrapPatternIndex pattern(exact.SourceVoxels(), source);
    WrapCollisionTracker tracker;

    const auto check = [&](const SelectionBounds target,
        const VoxelWrapOptions& options, const char* const step)
    {
        const VoxelWrapSummary summary =
            WrapVoxelSelectionOperation::Summarize(pattern, target, options);
        Require(summary.Valid(), "Tracker step summary is invalid.");
        const WrapCollisionState state = tracker.Update(
            document, 0U, pattern, target, options, summary.DestinationCount);
        const VoxelWrapGeometry geometry =
            WrapVoxelSelectionOperation::BuildGeometry(pattern, target, options);
        Require(exact.SetExplicitVoxelDestinations(
            document, selection, Generation, geometry.Destinations) ||
            exact.VoxelCount() == geometry.Destinations.size(),
            "Tracker exact preview could not update.");
        Require(state.Count == exact.CollisionCount(),
            std::string("Tracker collision count differs from the exact "
                "model at step: ") + step);
        if (state.Count > 0U)
        {
            // Bornes exactes tant que les positions sont suivies.
            const auto positions = exact.CollisionPositions();
            Position minimum = positions.front(), maximum = positions.front();
            for (const Position p : positions)
            {
                minimum.X = std::min(minimum.X, p.X); maximum.X = std::max(maximum.X, p.X);
                minimum.Y = std::min(minimum.Y, p.Y); maximum.Y = std::max(maximum.Y, p.Y);
                minimum.Z = std::min(minimum.Z, p.Z); maximum.Z = std::max(maximum.Z, p.Z);
            }
            Require(state.Bounds == SelectionBounds::FromCorners(minimum, maximum),
                std::string("Tracker collision bounds differ at step: ") + step);
        }
    };
    VoxelWrapOptions plain;
    // Croissance X face par face jusqu'a traverser le mur, puis retrait.
    for (std::int32_t x = 3; x <= 30; ++x)
    {
        check(SelectionBounds::FromCorners({0, 0, 0}, {x, 5, 5}), plain, "grow");
        // Preuve du chemin incremental : une tranche de 1 x 6 x 6 = 36
        // destinations visitees, jamais le resultat entier ni le document.
        Require(x == 3 || tracker.LastVisited() <= 36U,
            "Growing by one slab visited more than the slab.");
    }
    for (std::int32_t x = 30; x >= 4; x -= 3)
    {
        check(SelectionBounds::FromCorners({0, 0, 0}, {x, 5, 5}), plain, "shrink");
        // Retrait avec positions suivies : aucune destination visitee.
        Require(tracker.LastVisited() == 0U,
            "Shrinking with tracked positions visited destinations.");
    }
    // Croissance Y (autre face) puis changement d'options (recalcul).
    check(SelectionBounds::FromCorners({0, 0, 0}, {20, 11, 5}), plain, "grow y");
    VoxelWrapOptions spaced;
    spaced.X.Spacing = 1;
    check(SelectionBounds::FromCorners({0, 0, 0}, {20, 11, 5}), spaced, "options");
    check(SelectionBounds::FromCorners({0, 0, 0}, {26, 11, 11}), spaced, "jump");
    spaced.X.MirrorRepeat = true;
    check(SelectionBounds::FromCorners({0, 0, 0}, {26, 11, 11}), spaced, "mirror");
    check(SelectionBounds::FromCorners({0, 0, 0}, {2, 1, 1}), spaced, "collapse");
}

// --- 5. Materialisation par producteur == materialisation explicite --------

void TestGeneratedDestinationsMatchExplicit()
{
    auto document = MakeDocument({{2U, 1U, 1U, 3U}, {3U, 1U, 1U, 11U}, {4U, 4U, 4U, 5U}});
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    const std::vector<Position> selected{{2, 1, 1}, {3, 1, 1}};
    const SelectionBounds source = SelectionBounds::FromCorners({2, 1, 1}, {3, 1, 1});
    Require(selection.ApplySortedVolume(selected, source, SelectionMode::Replace),
        "Unable to select the generator pattern.");
    const SelectionBounds target = SelectionBounds::FromCorners({2, 1, 1}, {9, 4, 4});
    VoxelWrapOptions options;
    options.Y.Spacing = 1;
    TransformPreviewModel explicitModel, generatedModel;
    Require(explicitModel.BeginPreview(document, selection, Generation) &&
        generatedModel.BeginPreview(document, selection, Generation),
        "Generator previews could not begin.");
    const WrapPatternIndex pattern(explicitModel.SourceVoxels(), source);
    const VoxelWrapGeometry geometry =
        WrapVoxelSelectionOperation::BuildGeometry(pattern, target, options);
    Require(explicitModel.SetExplicitVoxelDestinations(
        document, selection, Generation, geometry.Destinations),
        "Explicit materialisation failed.");
    Require(generatedModel.SetGeneratedVoxelDestinations(
        document, selection, Generation, geometry.Destinations.size(),
        [&](const TransformPreviewDestinationSink& sink)
        {
            WrapVoxelSelectionOperation::ForEachDestination(
                pattern, target, options, sink);
        }),
        "Generated materialisation failed.");
    Require(explicitModel.VoxelCount() == generatedModel.VoxelCount() &&
        explicitModel.PreviewBounds() == generatedModel.PreviewBounds() &&
        explicitModel.CollisionCount() == generatedModel.CollisionCount() &&
        std::equal(explicitModel.Voxels().begin(), explicitModel.Voxels().end(),
            generatedModel.Voxels().begin()),
        "Generated and explicit materialisations differ.");
    // Un compte annonce faux ou une source inconnue laisse une preview
    // identite valide, jamais un tampon partiel.
    Require(!generatedModel.SetGeneratedVoxelDestinations(
        document, selection, Generation, 3U,
        [&](const TransformPreviewDestinationSink& sink)
        {
            sink({{2, 1, 1}, {2, 1, 1}, VoxelValue{3U}});
        }),
        "A short generator was accepted.");
    Require(generatedModel.IsActive() && generatedModel.VoxelCount() == 2U &&
        !generatedModel.HasExpandedDestinations(),
        "A failed generator did not restore an identity preview.");
    // Le mode compact ne peut jamais etre committe sans materialisation.
    TransformPreviewCompactSummary compact;
    compact.DestinationCount = 40U;
    compact.PreviewBounds = target;
    Require(generatedModel.SetCompactDestinations(document, selection, Generation, compact),
        "Compact summary was refused.");
    Require(generatedModel.IsCompact() && generatedModel.VoxelCount() == 40U &&
        generatedModel.Voxels().empty() &&
        !generatedModel.RenderData().Plan.DrawIndividualVoxels,
        "Compact mode did not report counts without voxels.");
    TransformOperationRequest request;
    request.Name = "Test"; request.Label = "Test";
    request.DestinationBounds = target;
    Require(!TransformOperationBuilder::Build(
        document, selection, Generation, generatedModel, request).Ready(),
        "A compact preview was committed without materialisation.");
}
} // namespace

int main()
{
    try
    {
        TestEmptyEdgeSourceBoundsPreviewEqualsCommit();
        TestUndoRedoBetweenWrapsIsTransparent();
        TestFrameworkEditableBoundsAreOptionalAndValidated();
        TestSummaryMatchesMaterialisedGeometry();
        TestCollisionTrackerMatchesExactModel();
        TestGeneratedDestinationsMatchExplicit();
        std::cout << "Wrap commit tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Wrap commit tests failed: " << exception.what() << '\n';
        return 1;
    }
}
