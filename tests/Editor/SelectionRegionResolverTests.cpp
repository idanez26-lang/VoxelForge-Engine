// VF-UX-SELECTION-V1 — tests du resolveur Region et des regles pures.
//
// Chaque exigence de la mission est mesuree sur les POSITIONS produites, pas
// sur les enums : region connectee simple, separation par le vide, Face contre
// Volume, couleur, et le cas ou 4 et 8 divergent volontairement.

#include "Selection/SelectionRegionResolver.h"
#include "Selection/SelectionInteraction.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace VoxelForge::Editor;
using Position = VoxelForge::Asset::Voxel::VoxelPosition;

struct PositionHash final
{
    std::size_t operator()(const Position p) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(p.X)) ^
            (static_cast<std::size_t>(p.Y) << 11U) ^
            (static_cast<std::size_t>(p.Z) << 22U);
    }
};
using World = std::unordered_map<Position, std::uint8_t, PositionHash>;

void Require(const bool value, const std::string_view message)
{
    if (!value) throw std::runtime_error(std::string(message));
}

SelectionRegionRequest Request(const World& world, const Position seed)
{
    SelectionRegionRequest request;
    request.Seed = seed;
    request.ReadVoxel = [&world](const Position p)
        -> std::optional<std::uint8_t>
    {
        const auto found = world.find(p);
        if (found == world.end()) return std::nullopt;
        return found->second;
    };
    request.ForEachVoxel = [&world](
        const std::function<void(Position, std::uint8_t)>& visit)
    {
        for (const auto& [position, color] : world) visit(position, color);
    };
    return request;
}

bool Contains(const SelectionRegionResult& result, const Position p)
{
    for (const Position q : result.Positions)
        if (q.X == p.X && q.Y == p.Y && q.Z == p.Z) return true;
    return false;
}

// --- Region Geometry : connectee, et separee par le vide ------------------

void TestGeometryRegionAndVoidSeparation()
{
    World world;
    // Ilot A : barre de 3, couleurs melangees. Ilot B : separe par du vide.
    world[{0, 0, 0}] = 1U;
    world[{1, 0, 0}] = 2U;
    world[{2, 0, 0}] = 1U;
    world[{5, 0, 0}] = 1U;   // ilot B
    world[{6, 0, 0}] = 1U;

    SelectionRegionRequest request = Request(world, {0, 0, 0});
    request.Target = SelectionRegionTarget::Volume;
    request.Criterion = SelectionRegionCriterion::Geometry;
    const SelectionRegionResult region = ResolveSelectionRegion(request);
    Require(region.Positions.size() == 3U,
        "Geometry doit couvrir l'ilot connecte entier, couleurs confondues.");
    Require(!Contains(region, {5, 0, 0}) && !Contains(region, {6, 0, 0}),
        "La propagation a traverse le vide.");

    // Graine dans le vide : region vide, pas d'erreur.
    SelectionRegionRequest empty = Request(world, {9, 9, 9});
    Require(ResolveSelectionRegion(empty).Empty(),
        "Une graine hors matiere doit rendre une region vide.");
}

// --- Region Color : limitee a la couleur de la graine ---------------------

void TestColorCriterionStopsAtColorBoundary()
{
    World world;
    world[{0, 0, 0}] = 1U;
    world[{1, 0, 0}] = 1U;
    world[{2, 0, 0}] = 2U;   // frontiere de couleur
    world[{3, 0, 0}] = 1U;   // meme couleur mais DERRIERE la frontiere

    SelectionRegionRequest request = Request(world, {0, 0, 0});
    request.Target = SelectionRegionTarget::Volume;
    request.Criterion = SelectionRegionCriterion::Color;
    const SelectionRegionResult region = ResolveSelectionRegion(request);
    Require(region.Positions.size() == 2U &&
        Contains(region, {0, 0, 0}) && Contains(region, {1, 0, 0}),
        "Color doit s'arreter a la frontiere de couleur.");
    Require(!Contains(region, {3, 0, 0}),
        "Color a saute par-dessus une couleur etrangere : la continuite "
        "geometrique n'a pas ete respectee.");
}

// --- Cible Color : globale, sans connectivite -----------------------------

void TestColorTargetIsGlobal()
{
    World world;
    world[{0, 0, 0}] = 3U;
    world[{9, 9, 9}] = 3U;   // deconnecte, meme couleur
    world[{1, 0, 0}] = 1U;

    SelectionRegionRequest request = Request(world, {0, 0, 0});
    request.Target = SelectionRegionTarget::Color;
    const SelectionRegionResult region = ResolveSelectionRegion(request);
    Require(region.Positions.size() == 2U && Contains(region, {9, 9, 9}),
        "La cible Color doit atteindre les voxels deconnectes de meme "
        "couleur.");
    Require(!Contains(region, {1, 0, 0}),
        "La cible Color a pris une couleur etrangere.");
}

// --- Face contre Volume ----------------------------------------------------

void TestFaceStaysOnTheVisibleLayer()
{
    World world;
    // Plateau 3x3 en Y=1, pose sur un plateau 3x3 en Y=0 : seul le plateau
    // superieur est expose vers +Y.
    for (int x = 0; x < 3; ++x)
        for (int z = 0; z < 3; ++z)
        {
            world[{x, 0, z}] = 1U;
            world[{x, 1, z}] = 1U;
        }

    SelectionRegionRequest face = Request(world, {1, 1, 1});
    face.Target = SelectionRegionTarget::Face;
    face.NormalX = 0.0F; face.NormalY = 1.0F; face.NormalZ = 0.0F;
    const SelectionRegionResult top = ResolveSelectionRegion(face);
    Require(top.Positions.size() == 9U,
        "Face doit couvrir la couche visible entiere.");
    for (const Position p : top.Positions)
        Require(p.Y == 1, "Face a quitte sa couche.");

    SelectionRegionRequest volume = Request(world, {1, 1, 1});
    volume.Target = SelectionRegionTarget::Volume;
    const SelectionRegionResult all = ResolveSelectionRegion(volume);
    Require(all.Positions.size() == 18U,
        "Volume doit traverser les deux couches connectees.");

    // Graine ENTERREE (couche 0, couverte par la couche 1) : la face +Y n'y
    // est pas visible, la region Face est donc vide — pas d'invention.
    SelectionRegionRequest buried = Request(world, {1, 0, 1});
    buried.Target = SelectionRegionTarget::Face;
    buried.NormalY = 1.0F;
    Require(ResolveSelectionRegion(buried).Empty(),
        "Une graine enterree n'a pas de face visible a selectionner.");
}

// --- 4 contre 8 : divergence voulue ---------------------------------------

void TestPlanarConnectivityFourVersusEight()
{
    World world;
    // Deux carres relies UNIQUEMENT par une diagonale en Y=0, tous exposes.
    world[{0, 0, 0}] = 1U;
    world[{1, 0, 1}] = 1U;   // contact diagonal avec (0,0)

    SelectionRegionRequest four = Request(world, {0, 0, 0});
    four.Target = SelectionRegionTarget::Face;
    four.NormalY = 1.0F;
    four.Planar = SelectionPlanarConnectivity::Four;
    Require(ResolveSelectionRegion(four).Positions.size() == 1U,
        "En voisinage 4, la diagonale ne relie pas.");

    SelectionRegionRequest eight = four;
    eight.Planar = SelectionPlanarConnectivity::Eight;
    Require(ResolveSelectionRegion(eight).Positions.size() == 2U,
        "En voisinage 8, la diagonale relie.");
}

// --- 6 contre 26 : l'analogue volumique, nomme pour ce qu'il est ----------

void TestVolumeConnectivitySixVersusTwentySix()
{
    World world;
    world[{0, 0, 0}] = 1U;
    world[{1, 1, 1}] = 1U;   // contact par le coin uniquement

    SelectionRegionRequest six = Request(world, {0, 0, 0});
    six.Target = SelectionRegionTarget::Volume;
    six.Volume = SelectionVolumeConnectivity::Six;
    Require(ResolveSelectionRegion(six).Positions.size() == 1U,
        "En voisinage 6, un contact par coin ne relie pas.");

    SelectionRegionRequest twentySix = six;
    twentySix.Volume = SelectionVolumeConnectivity::TwentySix;
    Require(ResolveSelectionRegion(twentySix).Positions.size() == 2U,
        "En voisinage 26, un contact par coin relie.");
}

// --- Regle Rect : une couche, sans contamination --------------------------

void TestRectFlattening()
{
    SelectionBounds bounds = SelectionBounds::FromCorners({2, 3, 4}, {7, 9, 6});
    const SelectionBounds flatY = FlattenSelectionBoundsToLayer(bounds, 1, 3);
    Require(flatY.Minimum.Y == 3 && flatY.Maximum.Y == 3 &&
        flatY.Minimum.X == 2 && flatY.Maximum.X == 7 &&
        flatY.Minimum.Z == 4 && flatY.Maximum.Z == 6,
        "L'aplatissement Y doit garder l'etendue X/Z et une couche Y.");
    const SelectionBounds flatX = FlattenSelectionBoundsToLayer(bounds, 0, 5);
    Require(flatX.Minimum.X == 5 && flatX.Maximum.X == 5,
        "L'aplatissement X doit produire une couche X unique.");
    const SelectionBounds flatZ = FlattenSelectionBoundsToLayer(bounds, 2, 4);
    Require(flatZ.Minimum.Z == 4 && flatZ.Maximum.Z == 4,
        "L'aplatissement Z doit produire une couche Z unique.");
    // L'axe vient de la normale visee, jamais d'un etat precedent.
    Require(DominantAxisOf(0.0F, 1.0F, 0.0F) == 1 &&
        DominantAxisOf(-1.0F, 0.2F, 0.1F) == 0 &&
        DominantAxisOf(0.1F, 0.2F, -0.9F) == 2,
        "L'axe dominant de la normale est mal resolu.");
}

// --- Visibilite contextuelle : la regle formalisee ------------------------

void TestContextualVisibilityRules()
{
    Require(RegionShowsCriterion(SelectionRegionTarget::Volume) &&
        RegionShowsCriterion(SelectionRegionTarget::Face) &&
        !RegionShowsCriterion(SelectionRegionTarget::Color),
        "Le critere n'a pas de sens pour la cible Color : il EST la couleur.");
    Require(RegionShowsPlanarConnectivity(SelectionRegionTarget::Face) &&
        !RegionShowsPlanarConnectivity(SelectionRegionTarget::Volume) &&
        !RegionShowsPlanarConnectivity(SelectionRegionTarget::Color),
        "Le voisinage 4/8 n'a de sens que dans un plan.");
    Require(RegionShowsVolumeConnectivity(SelectionRegionTarget::Volume) &&
        !RegionShowsVolumeConnectivity(SelectionRegionTarget::Face) &&
        !RegionShowsVolumeConnectivity(SelectionRegionTarget::Color),
        "Le voisinage 6/26 n'a de sens qu'en volume.");
}

// --- Rect : PREVIEW == COMMIT ----------------------------------------------
//
// Conduit le VRAI SelectionInteraction (PointerDown / PointerMove / PointerUp)
// et applique aux deux extremites la MEME regle que les deux sites reels :
// ResolveSelectionGestureBounds. Si la preview redevenait volumique — par
// exemple si la regle cessait d'aplatir en mode Rect — l'assertion
// « une seule couche » echoue.

void TestRectPreviewEqualsCommit()
{
    struct Orientation final
    {
        std::string_view Name;
        int Axis;
        std::int32_t Layer;
        Position Anchor;
        Position Target;
    };
    const Orientation orientations[] = {
        {"plan Y", 1, 3, {2, 3, 4}, {7, 9, 6}},
        {"plan X", 0, 2, {2, 3, 4}, {8, 6, 7}},
        {"plan Z", 2, 4, {2, 3, 4}, {6, 8, 9}},
    };

    for (const Orientation& o : orientations)
    {
        // Drag dans les deux directions : aller et retour.
        const std::pair<Position, Position> directions[] = {
            {o.Anchor, o.Target}, {o.Target, o.Anchor}};
        for (const auto& [from, to] : directions)
        {
            SelectionInteraction interaction;
            Require(interaction.PointerDown(from, 1U, SelectionMode::Replace,
                        0.0F, 0.0F),
                "Le geste Rect n'a pas demarre.");
            static_cast<void>(interaction.PointerMove(64.0F, 64.0F, to));
            Require(interaction.IsDragRecognized(),
                "Le drag n'a pas ete reconnu.");

            // PREVIEW : la formule exacte du site des highlights.
            const std::int32_t layer = o.Axis == 0 ? from.X
                : o.Axis == 2 ? from.Z : from.Y;
            const SelectionBounds preview = ResolveSelectionGestureBounds(
                SelectionFamilyMode::Rect, interaction.CurrentBounds(),
                o.Axis, layer);

            // COMMIT : la formule exacte du site du relachement.
            const SelectionPointerRelease release = interaction.PointerUp();
            Require(release.WasDrag && release.Bounds.has_value(),
                "Le relachement n'a pas produit de bornes.");
            const SelectionBounds commit = ResolveSelectionGestureBounds(
                SelectionFamilyMode::Rect, *release.Bounds, o.Axis, layer);

            const std::string w = std::string(o.Name) + " : ";
            // Une SEULE couche, en preview ET au commit.
            const auto layerSpan = [axis = o.Axis](const SelectionBounds& b)
            {
                return axis == 0 ? b.Maximum.X - b.Minimum.X
                     : axis == 2 ? b.Maximum.Z - b.Minimum.Z
                                 : b.Maximum.Y - b.Minimum.Y;
            };
            Require(preview.Valid && layerSpan(preview) == 0,
                w + "la preview Rect n'est pas une couche unique.");
            Require(commit.Valid && layerSpan(commit) == 0,
                w + "le commit Rect n'est pas une couche unique.");
            // Et strictement les MEMES bornes.
            Require(preview.Minimum == commit.Minimum &&
                    preview.Maximum == commit.Maximum,
                w + "preview et commit Rect divergent.");
            // Box, lui, reste volumique : la regle est ciblee, pas globale.
            const SelectionBounds box = ResolveSelectionGestureBounds(
                SelectionFamilyMode::Box, commit, o.Axis, layer);
            Require(box.Minimum == commit.Minimum &&
                    box.Maximum == commit.Maximum,
                w + "la regle Rect a fui sur le mode Box.");
        }
    }

    // Le garde-fou anti-regression : si la regle cessait d'aplatir, ce test
    // le verrait. Un volume 3D passe en mode Rect DOIT perdre son epaisseur.
    const SelectionBounds volume =
        SelectionBounds::FromCorners({0, 0, 0}, {5, 5, 5});
    const SelectionBounds flattened = ResolveSelectionGestureBounds(
        SelectionFamilyMode::Rect, volume, 1, 2);
    Require(flattened.Maximum.Y - flattened.Minimum.Y == 0 &&
        flattened.Minimum.Y == 2,
        "Un volume passe en mode Rect doit etre aplati sur la couche capturee.");
}

// --- Le resultat alimente les quatre operations ---------------------------

void TestResolverFeedsSelectionOperations()
{
    World world;
    for (int x = 0; x < 4; ++x) world[{x, 0, 0}] = 1U;

    SelectionRegionRequest request = Request(world, {0, 0, 0});
    request.Target = SelectionRegionTarget::Volume;
    const SelectionRegionResult region = ResolveSelectionRegion(request);
    Require(region.Positions.size() == 4U, "Region attendue de 4 cellules.");

    SelectionService service;
    Require(service.Apply(region.Positions, SelectionMode::Replace) &&
        service.Count() == 4U, "Replace doit poser la region.");
    const std::vector<Position> extra{{9, 9, 9}};
    Require(service.Apply(extra, SelectionMode::Add) && service.Count() == 5U,
        "Add doit etendre la selection.");
    const std::vector<Position> half{{0, 0, 0}, {1, 0, 0}};
    Require(service.Apply(half, SelectionMode::Subtract) &&
        service.Count() == 3U, "Subtract doit retirer les cellules donnees.");
    Require(service.Apply(region.Positions, SelectionMode::Intersect) &&
        service.Count() == 2U,
        "Intersect doit garder l'intersection avec la region.");
    Require(!service.Contains({9, 9, 9}),
        "Intersect a conserve une cellule hors region.");

    // Correctif valide par Tony : les bornes editables suivent la selection
    // FINALE apres chaque operation.
    service.AlignEditableBoundsToSelection();
    Require(service.EditableBounds() == service.Bounds(),
        "AlignEditableBoundsToSelection n'aligne pas sur la selection finale.");
}

// --- Correctifs : clic vide et marquee ------------------------------------

void TestEmptyClickAndMarqueeRules()
{
    // Semantique du clic vide, par operation capturee.
    Require(EmptyClickClearsSelection(SelectionMode::Replace) &&
        EmptyClickClearsSelection(SelectionMode::Intersect),
        "Replace et Intersect sur du vide doivent vider la selection.");
    Require(!EmptyClickClearsSelection(SelectionMode::Add) &&
        !EmptyClickClearsSelection(SelectionMode::Subtract),
        "Add et Subtract sur du vide ne doivent rien changer.");
    // Region ne demarre jamais le marquee ; Box et Rect si.
    Require(!SelectionModeStartsMarquee(SelectionFamilyMode::Region),
        "Region est un outil de clic : pas de marquee.");
    Require(SelectionModeStartsMarquee(SelectionFamilyMode::Box) &&
        SelectionModeStartsMarquee(SelectionFamilyMode::Rect),
        "Box et Rect utilisent le marquee.");
}
}

int main()
{
    try
    {
        TestGeometryRegionAndVoidSeparation();
        TestColorCriterionStopsAtColorBoundary();
        TestColorTargetIsGlobal();
        TestFaceStaysOnTheVisibleLayer();
        TestPlanarConnectivityFourVersusEight();
        TestVolumeConnectivitySixVersusTwentySix();
        TestRectFlattening();
        TestContextualVisibilityRules();
        TestRectPreviewEqualsCommit();
        TestEmptyClickAndMarqueeRules();
        TestResolverFeedsSelectionOperations();
        std::cout << "Selection region resolver tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
