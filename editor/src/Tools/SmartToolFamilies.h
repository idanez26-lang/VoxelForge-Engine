#pragma once

// VF-UX-TOOLS (correctif Codex) â€” correspondance CANONIQUE entre les familles
// exposees dans la barre et l'etat interne du Smart Tool.
//
// Pourquoi un fichier a part : la barre et le panneau doivent appliquer la
// MEME regle. Tant qu'elle etait ecrite deux fois, deux etats internes
// pouvaient s'afficher sous le meme libelle â€” c'est exactement le defaut
// releve : Â« Cube Â» designait a la fois SingleVoxel et CubeBrush, dont l'un
// produit une forme plane et l'autre une dalle volumique.
//
// Ces fonctions sont pures et sans dependance a l'interface : elles se testent
// sans ImGui, sans editeur et sans GPU.

#include "SmartTools/SmartTool.h"

namespace VoxelForge::Editor
{

// Familles telles que la barre les presente.
enum class SmartToolFamily : std::uint8_t
{
    Pencil,
    Geometry,
    Face,
    Surface,
    Fill
};

// Modes de la famille Geometry. Cylinder est conserve pour compatibilite mais
// n'est pas un mode V1 : on ne l'introduit jamais, on se contente de ne pas le
// detruire quand il est deja actif.
enum class GeometryShape : std::uint8_t
{
    Line,
    Cube,
    Sphere,
    Cylinder
};

struct SmartToolSelection final
{
    SmartGeometry Geometry = SmartGeometry::Pencil;
    SmartToolMode Mode = SmartToolMode::SingleVoxel;

    [[nodiscard]] constexpr bool operator==(
        const SmartToolSelection&) const noexcept = default;
};

// --- Lecture de l'etat ----------------------------------------------------

[[nodiscard]] constexpr SmartToolFamily FamilyOf(
    const SmartGeometry geometry) noexcept
{
    switch (geometry)
    {
    case SmartGeometry::Face: return SmartToolFamily::Face;
    case SmartGeometry::Surface: return SmartToolFamily::Surface;
    case SmartGeometry::Fill: return SmartToolFamily::Fill;
    // Line est un MODE de la famille Geometry cote interface, alors qu'il
    // reste une geometrie distincte cote moteur.
    case SmartGeometry::Line:
    case SmartGeometry::Geometry: return SmartToolFamily::Geometry;
    default: return SmartToolFamily::Pencil;
    }
}

// Nom UX de la famille. Ici aussi, une seule table : le panneau derivait le
// libelle par une chaine de comparaisons qui redisait FamilyOf a sa maniere.
[[nodiscard]] constexpr const char* SmartToolFamilyName(
    const SmartToolFamily family) noexcept
{
    switch (family)
    {
    case SmartToolFamily::Geometry: return "Geometry";
    case SmartToolFamily::Face: return "Face";
    case SmartToolFamily::Surface: return "Surface";
    case SmartToolFamily::Fill: return "Fill";
    case SmartToolFamily::Pencil:
    default: return "Pencil";
    }
}

[[nodiscard]] constexpr GeometryShape GeometryShapeOf(
    const SmartGeometry geometry, const SmartToolMode mode) noexcept
{
    if (geometry == SmartGeometry::Line) return GeometryShape::Line;
    switch (mode)
    {
    case SmartToolMode::SphereBrush: return GeometryShape::Sphere;
    case SmartToolMode::CylinderBrush: return GeometryShape::Cylinder;
    // SingleVoxel ET CubeBrush s'affichaient tous deux Â« Cube Â». Seul le
    // premier est canonique ; le second est normalise a l'entree.
    default: return GeometryShape::Cube;
    }
}

// --- Representation canonique ---------------------------------------------
//
// Geometry Cube == SmartToolMode::SingleVoxel.
//
// Preuve, SmartToolPlanner.cpp :
//   l.645-648  SingleVoxel et CubeBrush -> ResolveSamples(rectangle plan)
//   l.1356-1359 SingleVoxel  -> Shape=Cube, Size FORCE a 1
//   l.1360-1362 CubeBrush    -> Shape=Cube, Size laisse a celui du Pencil
//   l.1347-1351 Geometry     -> Dimension = Volume3D
//
// CubeBrush dilate donc chaque cellule du rectangle par une brosse cubique 3D
// de taille N : le resultat est une dalle d'epaisseur N, dependante de l'outil
// precedemment actif. SingleVoxel force Size=1, ce qui redonne exactement le
// rectangle plan. C'est la seule des deux qui satisfait le contrat produit.
inline constexpr SmartToolMode kCanonicalGeometryCubeMode =
    SmartToolMode::SingleVoxel;
inline constexpr SmartToolMode kCanonicalGeometrySphereMode =
    SmartToolMode::SphereBrush;
inline constexpr GeometryShape kDefaultGeometryShape = GeometryShape::Cube;

// Line n'a qu'UNE semantique visible : Line + Thickness. Laisser passer un
// SphereBrush ou un CylinderBrush herite ferait dependre le trait d'une forme
// que l'interface ne montre plus — un etat cache, donc interdit.
//
// La paire canonique est {SingleVoxel, CubeBrush} : c'est la seule qui exprime
// « fin » et « epais » le long du chemin Line, lequel passe par ResolveSamples
// (SmartToolPlanner.cpp:485) et applique donc l'empreinte a chaque echantillon.
// CubeBrush est conserve tel quel — c'est un choix d'epaisseur explicite, pas
// un heritage — tandis que Sphere et Cylinder retombent sur le trait fin.
[[nodiscard]] constexpr SmartToolMode CanonicalLineMode(
    const SmartToolMode currentMode) noexcept
{
    return currentMode == SmartToolMode::CubeBrush
        ? SmartToolMode::CubeBrush : SmartToolMode::SingleVoxel;
}

// L'epaisseur telle que l'interface la montre, et la selection qui la realise.
// Une seule regle, pour que le controle Thickness ne puisse pas mentir.
[[nodiscard]] constexpr int LineThicknessOf(
    const SmartToolMode mode, const int brushSize) noexcept
{
    return CanonicalLineMode(mode) == SmartToolMode::SingleVoxel
        ? 1 : brushSize;
}

struct LineThickness final
{
    SmartToolMode Mode = SmartToolMode::SingleVoxel;
    int Size = 1;

    [[nodiscard]] constexpr bool operator==(
        const LineThickness&) const noexcept = default;
};

[[nodiscard]] constexpr LineThickness ApplyLineThickness(
    const int thickness) noexcept
{
    return thickness <= 1
        ? LineThickness{SmartToolMode::SingleVoxel, 1}
        : LineThickness{SmartToolMode::CubeBrush, thickness};
}

// `currentMode` n'est consulte que par Line, et seulement pour distinguer un
// trait epais deja choisi d'un mode herite. Cube et Sphere ignorent totalement
// le mode entrant : c'est ce qui les rend independants de l'outil precedent.
[[nodiscard]] constexpr SmartToolSelection ApplyGeometryShape(
    const GeometryShape shape,
    const SmartToolMode currentMode = kCanonicalGeometryCubeMode) noexcept
{
    switch (shape)
    {
    case GeometryShape::Line:
        return {SmartGeometry::Line, CanonicalLineMode(currentMode)};
    case GeometryShape::Sphere:
        return {SmartGeometry::Geometry, kCanonicalGeometrySphereMode};
    case GeometryShape::Cylinder:
        return {SmartGeometry::Geometry, SmartToolMode::CylinderBrush};
    case GeometryShape::Cube:
    default:
        return {SmartGeometry::Geometry, kCanonicalGeometryCubeMode};
    }
}

// Applique une famille SANS casser un sous-mode deja valide.
//
// Deux exigences se rencontrent ici :
//   - idempotence : recliquer la famille active ne doit rien changer ;
//   - canonicite : un sous-mode NON canonique doit etre normalise.
// CubeBrush sous Geometry n'est pas Â« un sous-mode valide qu'on preserverait Â» :
// c'est precisement l'etat defectueux. Le normaliser n'est donc pas une
// mutation silencieuse, c'est la correction.
[[nodiscard]] constexpr SmartToolSelection ApplyFamily(
    const SmartToolFamily family,
    const SmartGeometry currentGeometry,
    const SmartToolMode currentMode) noexcept
{
    switch (family)
    {
    case SmartToolFamily::Face:
        return {SmartGeometry::Face, currentMode};
    case SmartToolFamily::Surface:
        return {SmartGeometry::Surface, currentMode};
    case SmartToolFamily::Fill:
        return {SmartGeometry::Fill, currentMode};
    case SmartToolFamily::Pencil:
        // Le mode du Pencil designe la forme de brosse 3D : on le conserve.
        return {SmartGeometry::Pencil, currentMode};
    case SmartToolFamily::Geometry:
    default:
        break;
    }
    if (FamilyOf(currentGeometry) == SmartToolFamily::Geometry)
        return ApplyGeometryShape(
            GeometryShapeOf(currentGeometry, currentMode), currentMode);
    // Entree depuis une autre famille : choix deterministe, jamais herite. Le
    // mode entrant n'est pas transmis, ce qui coupe la fuite depuis Pencil.
    return ApplyGeometryShape(kDefaultGeometryShape);
}

// --- Actions valides par famille -------------------------------------------
//
// Fill n'a que deux actions sensees : Erase (vider la region) et Paint
// (recolorer la region). Add n'a pas de cible — la region de Fill est definie
// par des voxels EXISTANTS (Connected exige une region de couleur, Plane une
// face visible : ResolveFill, SmartToolPlanner.cpp:681-708). Regle produit V1 :
// Add n'est pas propose pour Fill, ni meme grise.
[[nodiscard]] constexpr bool ActionIsValidFor(
    const SmartToolFamily family, const SmartAction action) noexcept
{
    if (family == SmartToolFamily::Fill)
        return action == SmartAction::Erase || action == SmartAction::Paint;
    return action == SmartAction::Add || action == SmartAction::Erase ||
        action == SmartAction::Paint;
}

// Entrer dans une famille conserve l'action si elle y est valide ; sinon le
// repli est DETERMINISTE : Paint, l'usage dominant de Fill. Jamais d'etat
// cache : la nouvelle action est immediatement visible dans le panneau.
[[nodiscard]] constexpr SmartAction NormalizeActionFor(
    const SmartToolFamily family, const SmartAction action) noexcept
{
    return ActionIsValidFor(family, action) ? action : SmartAction::Paint;
}

// --- Options reellement consommees ----------------------------------------
//
// Un controle actif doit agir. Verifie contre SmartToolPlanner :
//   Line     -> ResolveSamples (l.485)          : la brosse epaissit le trait
//   Geometry -> Size force a 1 (Cube) ou chemin direct (Sphere, Cylinder)
//   Surface  -> empreinte 2D sauf en Paint (l.864-868)
//   Face, Fill -> aucune empreinte
[[nodiscard]] constexpr bool ConsumesBrushSize(
    const SmartGeometry geometry, const SmartToolMode mode,
    const SmartAction action) noexcept
{
    switch (FamilyOf(geometry))
    {
    case SmartToolFamily::Pencil:
        return mode != SmartToolMode::SingleVoxel;
    case SmartToolFamily::Geometry:
        // Seul Line epaissit ; les formes 2D sont d'epaisseur 1 par contrat.
        return geometry == SmartGeometry::Line &&
            mode != SmartToolMode::SingleVoxel;
    case SmartToolFamily::Surface:
        return action != SmartAction::Paint &&
            mode != SmartToolMode::SingleVoxel;
    case SmartToolFamily::Face:
    case SmartToolFamily::Fill:
    default:
        return false;
    }
}

} // namespace VoxelForge::Editor
