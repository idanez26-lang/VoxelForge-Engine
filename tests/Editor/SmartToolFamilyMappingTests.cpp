// VF-UX-TOOLS (correctif Codex) — la famille Geometry produit-elle vraiment
// des formes 2D d'epaisseur 1 ?
//
// Ces tests evitent deliberement de recopier le mapping de l'implementation.
// Ils mesurent un INVARIANT OBSERVABLE : le nombre de plans distincts occupes
// par le plan genere, le long de la normale verrouillee. Une forme 2D en
// occupe exactement un ; une dalle volumique en occupe plusieurs. Le test
// resterait donc valable meme si toute la correspondance interne changeait.

#include "SmartTools/SmartToolController.h"
#include "Tools/SmartToolFamilies.h"

#include <cstdint>
#include <iostream>
#include <set>
#include <utility>
#include <tuple>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

struct Hash
{
    std::size_t operator()(const Position p) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(p.X)) ^
            (static_cast<std::size_t>(p.Y) << 11U) ^
            (static_cast<std::size_t>(p.Z) << 22U);
    }
};
using States = std::unordered_map<Position, SmartToolVoxelState, Hash>;

void Require(const bool value, const std::string_view message)
{
    if (!value) throw std::runtime_error(std::string(message));
}

// Plan reel produit par le planner, pour une selection (geometrie + mode)
// donnee et une taille de brosse donnee.
SmartToolPlanPtr PlanFor(const SmartToolSelection selection, const int brushSize,
    const Position start, const Position end)
{
    constexpr Position normal{0, 1, 0};
    const auto plane = SmartToolPlanner::MakeGeometryPlane(
        start, normal, static_cast<float>(start.Y));
    Require(plane.has_value(), "Le plan de geometrie n'a pas ete canonicalise.");

    const States states;
    SmartToolRequest request;
    request.Geometry = selection.Geometry;
    request.Mode = selection.Mode;
    request.Action = SmartAction::Add;
    request.GeometryHeight = 1;
    request.GeometryPlane = *plane;
    if (selection.Geometry == SmartGeometry::Line)
    {
        request.LineStart = start;
        request.GeometryPlane.reset();
    }
    request.BrushRequest = {{32U, 32U, 32U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
         SmartBrushOrientation::Auto, brushSize, 9U, SmartBrushMode::Add},
        {SmartToolPlanner::ProjectGeometryEndpoint(*plane, end), normal}, {}};
    request.ReadVoxel = [&states](const Position p)
    {
        const auto found = states.find(p);
        return found == states.end() ? SmartToolVoxelState{} : found->second;
    };
    request.SourceIdentity = 0x5EC8U;
    request.SourceRevision = 1U;

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Le planner n'a produit aucun plan.");
    return result.Plan;
}

// L'invariant observable : combien de plans distincts la forme occupe-t-elle
// le long de la normale ?
[[nodiscard]] std::size_t LayerCount(const SmartToolPlanPtr& plan)
{
    std::set<std::int32_t> layers;
    for (const SmartToolPlanCell& cell : plan->Cells())
        layers.insert(cell.WorldPosition.Y);
    return layers.size();
}

// --- A. l'action est orthogonale a la famille ----------------------------

void TestActionSurvivesFamilyChange()
{
    constexpr SmartToolFamily families[] = {
        SmartToolFamily::Pencil, SmartToolFamily::Geometry,
        SmartToolFamily::Face, SmartToolFamily::Surface,
        SmartToolFamily::Fill};
    constexpr SmartAction actions[] = {
        SmartAction::Add, SmartAction::Erase, SmartAction::Paint};
    for (const SmartAction action : actions)
    {
        SmartTool tool;
        tool.SetAction(action);
        for (const SmartToolFamily family : families)
        {
            const SmartToolSelection selection = ApplyFamily(
                family, tool.Geometry(), tool.Mode());
            tool.SetGeometry(selection.Geometry);
            tool.SetMode(selection.Mode);
            tool.SetAction(NormalizeActionFor(family, tool.Action()));
            // L'action survit tant qu'elle est VALIDE pour la famille. La
            // seule exception est produit : Fill n'a pas de Add.
            const SmartAction expected =
                family == SmartToolFamily::Fill && action == SmartAction::Add
                ? SmartAction::Paint : action;
            Require(tool.Action() == expected,
                "Changer de famille a modifie l'action courante.");
            Require(tool.IsOperational(),
                "Une famille a produit un etat non operationnel.");
        }
    }
}

// --- B, F. idempotence ----------------------------------------------------

void TestGeometryFamilyIsIdempotent()
{
    struct Case final
    {
        std::string_view Name;
        SmartToolSelection Start;
    };
    const Case cases[] = {
        {"Line", {SmartGeometry::Line, SmartToolMode::SingleVoxel}},
        {"Cube", {SmartGeometry::Geometry, SmartToolMode::SingleVoxel}},
        {"Sphere", {SmartGeometry::Geometry, SmartToolMode::SphereBrush}},
        {"Cylinder", {SmartGeometry::Geometry, SmartToolMode::CylinderBrush}},
    };
    for (const Case& item : cases)
    {
        const SmartToolSelection once = ApplyFamily(
            SmartToolFamily::Geometry, item.Start.Geometry, item.Start.Mode);
        Require(once == item.Start,
            std::string("Recliquer Geometry a mute le sous-mode ") +
                std::string(item.Name) + ".");
        const SmartToolSelection twice = ApplyFamily(
            SmartToolFamily::Geometry, once.Geometry, once.Mode);
        Require(twice == once, "ApplyFamily n'est pas idempotent.");
    }
}

// --- C. Geometry Cube canonique, quelle que soit la provenance -----------

void TestGeometryCubeIsCanonicalFromEveryOrigin()
{
    const SmartToolSelection origins[] = {
        {SmartGeometry::Pencil, SmartToolMode::SingleVoxel},
        {SmartGeometry::Pencil, SmartToolMode::CubeBrush},
        {SmartGeometry::Pencil, SmartToolMode::SphereBrush},
        {SmartGeometry::Face, SmartToolMode::CubeBrush},
        {SmartGeometry::Surface, SmartToolMode::SphereBrush},
        {SmartGeometry::Fill, SmartToolMode::CubeBrush},
    };
    SmartToolSelection reference{};
    bool first = true;
    for (const SmartToolSelection& origin : origins)
    {
        const SmartToolSelection entered = ApplyFamily(
            SmartToolFamily::Geometry, origin.Geometry, origin.Mode);
        if (first) { reference = entered; first = false; }
        Require(entered == reference,
            "Entrer dans Geometry depuis des etats differents ne donne pas "
            "la meme representation canonique.");
        Require(GeometryShapeOf(entered.Geometry, entered.Mode) ==
            GeometryShape::Cube,
            "Le mode Geometry par defaut n'est pas Cube.");
    }
}

// --- C, D, E. l'epaisseur reellement planifiee ---------------------------

void TestPlannedThickness()
{
    constexpr Position start{8, 8, 8};
    constexpr Position end{12, 8, 12};

    // Geometry Cube : une seule couche, quelle que soit la taille de brosse
    // heritee. C'est le coeur du defaut corrige : avec CubeBrush, une taille de
    // 3 produisait trois couches.
    const SmartToolSelection cube = ApplyGeometryShape(GeometryShape::Cube);
    for (const int size : {1, 3, 5})
    {
        Require(LayerCount(PlanFor(cube, size, start, end)) == 1U,
            "Geometry Cube n'est pas d'epaisseur 1 : la taille de brosse "
            "heritee fuit dans la forme.");
    }
    // Le mode NON canonique reste volumique — c'est ce qui prouve que le test
    // mesure bien quelque chose, et que la canonicalisation etait necessaire.
    const SmartToolSelection legacyCube{
        SmartGeometry::Geometry, SmartToolMode::CubeBrush};
    Require(LayerCount(PlanFor(legacyCube, 3, start, end)) > 1U,
        "Le mode non canonique devrait etre volumique ; sinon ce test ne "
        "distingue rien et la preuve d'epaisseur ne vaut rien.");

    // Les chiffres mesures sont affiches : un rapport qui dit « le test passe »
    // vaut moins qu'un rapport qui montre 1 contre 3 sur le meme planner.
    std::cout << "  Geometry Cube  (canonique, size 3) : "
              << LayerCount(PlanFor(cube, 3, start, end)) << " couche(s)\n"
              << "  Geometry Cube  (CubeBrush, size 3) : "
              << LayerCount(PlanFor(legacyCube, 3, start, end))
              << " couche(s)  <- le defaut corrige\n";

    // Geometry Sphere : disque plan, chemin direct sans empreinte.
    const SmartToolSelection sphere = ApplyGeometryShape(GeometryShape::Sphere);
    for (const int size : {1, 4})
    {
        Require(LayerCount(PlanFor(sphere, size, start, end)) == 1U,
            "Geometry Sphere n'est pas d'epaisseur 1.");
    }

    // Pencil Cube et Sphere restent des brosses 3D : plusieurs couches.
    const SmartToolSelection pencilCube{
        SmartGeometry::Pencil, SmartToolMode::CubeBrush};
    const SmartToolSelection pencilSphere{
        SmartGeometry::Pencil, SmartToolMode::SphereBrush};
    Require(LayerCount(PlanFor(pencilCube, 3, start, start)) > 1U,
        "Pencil Cube a perdu son volume 3D.");
    Require(LayerCount(PlanFor(pencilSphere, 3, start, start)) > 1U,
        "Pencil Sphere a perdu son volume 3D.");
    std::cout << "  Geometry Sphere (size 4)           : "
              << LayerCount(PlanFor(sphere, 4, start, end)) << " couche(s)\n"
              << "  Pencil Cube 3D  (size 3)           : "
              << LayerCount(PlanFor(pencilCube, 3, start, start))
              << " couche(s)\n"
              << "  Pencil Sphere 3D (size 3)          : "
              << LayerCount(PlanFor(pencilSphere, 3, start, start))
              << " couche(s)\n";
}

// --- G. Size : actif seulement s'il agit ---------------------------------

void TestBrushSizeIsOfferedOnlyWhenItActs()
{
    // Geometry : aucune taille consommee, sauf Line qui epaissit le trait.
    Require(!ConsumesBrushSize(SmartGeometry::Geometry,
        SmartToolMode::SingleVoxel, SmartAction::Add),
        "Geometry Cube ne doit pas offrir une taille qu'il ecrase.");
    Require(!ConsumesBrushSize(SmartGeometry::Geometry,
        SmartToolMode::SphereBrush, SmartAction::Add),
        "Geometry Sphere ne consomme pas la taille.");
    Require(ConsumesBrushSize(SmartGeometry::Line,
        SmartToolMode::CubeBrush, SmartAction::Add),
        "Line epaissit son trait par l'empreinte de brosse.");
    // Pencil : la taille agit des que le mode n'est pas SingleVoxel.
    Require(ConsumesBrushSize(SmartGeometry::Pencil,
        SmartToolMode::CubeBrush, SmartAction::Add) &&
        !ConsumesBrushSize(SmartGeometry::Pencil,
            SmartToolMode::SingleVoxel, SmartAction::Add),
        "La taille du Pencil est mal exposee.");
    // Surface : empreinte 2D sauf en Paint (SmartToolPlanner.cpp:864-868).
    Require(ConsumesBrushSize(SmartGeometry::Surface,
        SmartToolMode::CubeBrush, SmartAction::Add) &&
        !ConsumesBrushSize(SmartGeometry::Surface,
            SmartToolMode::CubeBrush, SmartAction::Paint),
        "Surface Paint ne consomme pas d'empreinte.");
    Require(!ConsumesBrushSize(SmartGeometry::Face,
        SmartToolMode::CubeBrush, SmartAction::Add) &&
        !ConsumesBrushSize(SmartGeometry::Fill,
            SmartToolMode::CubeBrush, SmartAction::Add),
        "Face et Fill n'utilisent aucune empreinte.");

    // Et la promesse inverse : quand la taille est annoncee active, elle agit.
    constexpr Position start{8, 8, 8};
    Require(LayerCount(PlanFor(
        {SmartGeometry::Pencil, SmartToolMode::CubeBrush}, 1, start, start)) <
        LayerCount(PlanFor(
            {SmartGeometry::Pencil, SmartToolMode::CubeBrush}, 5, start, start)),
        "Une taille annoncee active doit changer le resultat.");
}

// --- Line : une seule semantique visible, Line + Thickness ---------------
//
// Ces tests comparent les CELLULES produites, pas les enums. Deux entrees
// differentes qui donnent le meme trait doivent donner exactement le meme
// ensemble de voxels.

[[nodiscard]] std::set<std::tuple<int, int, int>> CellSet(
    const SmartToolPlanPtr& plan)
{
    std::set<std::tuple<int, int, int>> cells;
    for (const SmartToolPlanCell& cell : plan->Cells())
        cells.emplace(cell.WorldPosition.X, cell.WorldPosition.Y,
            cell.WorldPosition.Z);
    return cells;
}

// Section transversale du trait : son epaisseur observable.
[[nodiscard]] std::size_t CrossSection(const SmartToolPlanPtr& plan)
{
    std::set<std::pair<int, int>> section;
    for (const SmartToolPlanCell& cell : plan->Cells())
        section.emplace(cell.WorldPosition.Y, cell.WorldPosition.Z);
    return section.size();
}

// Le trajet reel de l'utilisateur : un etat precedent, un clic sur Line, puis
// un reglage d'epaisseur. Aucune etape n'est court-circuitee.
SmartToolPlanPtr LinePlanAfter(const SmartToolSelection previous,
    const int previousBrushSize, const int thickness)
{
    // 1. clic sur la forme Line depuis l'etat precedent
    const SmartToolSelection lineSelection =
        ApplyGeometryShape(GeometryShape::Line, previous.Mode);
    // 2. reglage de l'epaisseur dans le panneau
    const LineThickness applied = ApplyLineThickness(thickness);
    // La taille de brosse heritee subsiste dans l'etat ; si elle fuyait dans le
    // resultat, les cas A a D divergeraient.
    const int size = applied.Mode == SmartToolMode::SingleVoxel
        ? previousBrushSize : applied.Size;
    return PlanFor({lineSelection.Geometry, applied.Mode}, size,
        {4, 8, 8}, {12, 8, 8});
}

void TestLineIsDeterministic()
{
    struct Origin final
    {
        std::string_view Name;
        SmartToolSelection Selection;
        int BrushSize;
    };
    const Origin origins[] = {
        {"Geometry Sphere", {SmartGeometry::Geometry,
            SmartToolMode::SphereBrush}, 7},
        {"Geometry Cylinder", {SmartGeometry::Geometry,
            SmartToolMode::CylinderBrush}, 6},
        {"Geometry Cube", {SmartGeometry::Geometry,
            SmartToolMode::SingleVoxel}, 5},
        {"Pencil Sphere 3D", {SmartGeometry::Pencil,
            SmartToolMode::SphereBrush}, 4},
    };

    for (const int thickness : {1, 3, 5})
    {
        std::set<std::tuple<int, int, int>> reference;
        bool first = true;
        for (const Origin& origin : origins)
        {
            const SmartToolPlanPtr plan = LinePlanAfter(
                origin.Selection, origin.BrushSize, thickness);
            const auto cells = CellSet(plan);
            if (first) { reference = cells; first = false; }
            Require(cells == reference,
                std::string("Line depuis ") + std::string(origin.Name) +
                    " ne produit pas le meme trait : un etat precedent fuit.");
        }
    }

    // L'epaisseur doit reellement progresser, sinon le controle ment.
    const SmartToolSelection neutral{
        SmartGeometry::Geometry, SmartToolMode::SingleVoxel};
    const std::size_t thin = CrossSection(LinePlanAfter(neutral, 1, 1));
    const std::size_t medium = CrossSection(LinePlanAfter(neutral, 1, 3));
    const std::size_t thick = CrossSection(LinePlanAfter(neutral, 1, 5));
    std::cout << "  Line section : Thickness 1 = " << thin
              << ", 3 = " << medium << ", 5 = " << thick << '\n';
    Require(thin == 1U, "Thickness 1 doit donner un trait d'un voxel.");
    Require(medium > thin && thick > medium,
        "Thickness ne fait pas reellement varier l'epaisseur du trait.");

    // Le mode canonique : Sphere et Cylinder ne survivent jamais a Line.
    for (const SmartToolMode hidden : {SmartToolMode::SphereBrush,
        SmartToolMode::CylinderBrush})
    {
        const SmartToolSelection line =
            ApplyGeometryShape(GeometryShape::Line, hidden);
        Require(line.Mode == SmartToolMode::SingleVoxel,
            "Une forme de brosse heritee survit a l'activation de Line.");
    }
    // Un trait epais explicitement choisi, lui, est conserve : c'est une
    // decision de l'utilisateur, pas un heritage.
    Require(ApplyGeometryShape(GeometryShape::Line,
        SmartToolMode::CubeBrush).Mode == SmartToolMode::CubeBrush,
        "Un trait epais explicite doit survivre a un reclic sur Line.");
}

// --- FamilyOf est bien l'autorite unique ---------------------------------

void TestFamilyOfIsTheSingleAuthority()
{
    // La barre et le panneau posent la meme question ; ils doivent obtenir la
    // meme reponse, y compris sur le cas qui les faisait diverger.
    Require(FamilyOf(SmartGeometry::Line) == SmartToolFamily::Geometry &&
        FamilyOf(SmartGeometry::Geometry) == SmartToolFamily::Geometry,
        "Line et Geometry doivent appartenir a la meme famille UX.");
    Require(FamilyOf(SmartGeometry::Pencil) == SmartToolFamily::Pencil &&
        FamilyOf(SmartGeometry::Face) == SmartToolFamily::Face &&
        FamilyOf(SmartGeometry::Surface) == SmartToolFamily::Surface &&
        FamilyOf(SmartGeometry::Fill) == SmartToolFamily::Fill,
        "Une geometrie est classee dans la mauvaise famille.");
    // Le nom affiche vient de la meme table.
    Require(std::string_view(SmartToolFamilyName(
        FamilyOf(SmartGeometry::Line))) == "Geometry",
        "Le libelle de Line doit etre celui de sa famille.");
    // Toute geometrie operationnelle a une famille et un nom.
    for (const SmartGeometry geometry : {SmartGeometry::Pencil,
        SmartGeometry::Line, SmartGeometry::Geometry, SmartGeometry::Face,
        SmartGeometry::Surface, SmartGeometry::Fill})
    {
        Require(std::string_view(
            SmartToolFamilyName(FamilyOf(geometry))).size() > 0U,
            "Une famille sans nom affichable.");
    }
}

// --- Fill : Erase et Paint seulement --------------------------------------

void TestFillActionRule()
{
    Require(!ActionIsValidFor(SmartToolFamily::Fill, SmartAction::Add),
        "Fill ne doit pas proposer Add.");
    Require(ActionIsValidFor(SmartToolFamily::Fill, SmartAction::Erase) &&
        ActionIsValidFor(SmartToolFamily::Fill, SmartAction::Paint),
        "Fill doit proposer Erase et Paint.");
    Require(NormalizeActionFor(SmartToolFamily::Fill, SmartAction::Add) ==
        SmartAction::Paint,
        "Le repli de Fill doit etre deterministe : Paint.");
    // Les actions valides ne sont jamais alterees.
    Require(NormalizeActionFor(SmartToolFamily::Fill, SmartAction::Erase) ==
        SmartAction::Erase,
        "Une action valide pour Fill a ete alteree.");
    for (const SmartToolFamily family : {SmartToolFamily::Pencil,
        SmartToolFamily::Geometry, SmartToolFamily::Face,
        SmartToolFamily::Surface})
    {
        for (const SmartAction action : {SmartAction::Add, SmartAction::Erase,
            SmartAction::Paint})
        {
            Require(NormalizeActionFor(family, action) == action,
                "Une famille hors Fill a altere une action valide.");
        }
    }
}

// --- Les autres familles ne sont pas alterees ----------------------------

void TestOtherFamiliesPreserveTheirMode()
{
    constexpr SmartToolMode modes[] = {
        SmartToolMode::SingleVoxel, SmartToolMode::CubeBrush,
        SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush};
    for (const SmartToolMode mode : modes)
    {
        for (const auto [family, geometry] :
            {std::pair{SmartToolFamily::Pencil, SmartGeometry::Pencil},
             std::pair{SmartToolFamily::Face, SmartGeometry::Face},
             std::pair{SmartToolFamily::Surface, SmartGeometry::Surface},
             std::pair{SmartToolFamily::Fill, SmartGeometry::Fill}})
        {
            const SmartToolSelection entered =
                ApplyFamily(family, SmartGeometry::Pencil, mode);
            Require(entered.Geometry == geometry && entered.Mode == mode,
                "Une famille non-Geometry a altere le mode courant.");
        }
    }
}
}

int main()
{
    try
    {
        TestActionSurvivesFamilyChange();
        TestGeometryFamilyIsIdempotent();
        TestGeometryCubeIsCanonicalFromEveryOrigin();
        TestPlannedThickness();
        TestBrushSizeIsOfferedOnlyWhenItActs();
        TestFillActionRule();
        TestLineIsDeterministic();
        TestFamilyOfIsTheSingleAuthority();
        TestOtherFamiliesPreserveTheirMode();
        std::cout << "Smart Tool family mapping tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
