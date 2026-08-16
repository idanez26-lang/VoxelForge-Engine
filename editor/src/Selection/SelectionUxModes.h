#pragma once

// VF-UX-SELECTION-V1 â€” modes de la famille Selection et regles pures.
//
// Meme discipline que Tools/SmartToolFamilies.h : une seule autorite, pure et
// testable sans ImGui ni editeur, que le panneau ET le viewport consultent.
// Les regles de visibilite contextuelle sont FORMALISEES ici plutot que
// dispersees dans l'interface â€” une option affichee doit agir.

#include "Selection/SelectionService.h"

#include <cstdint>

namespace VoxelForge::Editor
{

// Les trois modes exposes par la famille Selection.
enum class SelectionFamilyMode : std::uint8_t
{
    Box,     // marquee volumique 3D existant
    Rect,    // rectangle planaire : UNE couche
    Region   // propagation depuis le voxel clique
};

// Portee de la propagation Region.
enum class SelectionRegionTarget : std::uint8_t
{
    Volume,  // traverse le volume connecte
    Face,    // reste sur la couche/face visee
    Color    // tous les voxels de la couleur cliquee, sans connectivite
};

// Critere de propagation.
enum class SelectionRegionCriterion : std::uint8_t
{
    Geometry, // continuite geometrique, couleurs confondues
    Color     // continuite geometrique ET meme couleur que la graine
};

// Voisinage PLANAIRE (cible Face) : les valeurs de la reference.
enum class SelectionPlanarConnectivity : std::uint8_t
{
    Four,   // aretes dans le plan
    Eight   // aretes + diagonales dans le plan
};

// Voisinage VOLUMIQUE (cible Volume). 4/8 n'a AUCUN sens en 3D : plutot que
// d'inventer une fausse correspondance silencieuse (regle produit Â§4), le
// voisinage volumique est nomme pour ce qu'il est.
enum class SelectionVolumeConnectivity : std::uint8_t
{
    Six,        // faces uniquement â€” l'analogue exact du 4 planaire
    TwentySix   // faces + aretes + coins â€” l'analogue du 8 planaire
};

struct SelectionToolOptions final
{
    SelectionFamilyMode Mode = SelectionFamilyMode::Box;
    // Operation de BASE, visible dans le panneau. Les modificateurs clavier
    // (Shift/Ctrl) continuent de primer pendant le geste : l'interface et les
    // raccourcis refletent la meme verite, le panneau affiche l'operation
    // effective.
    SelectionMode Operation = SelectionMode::Replace;
    SelectionRegionTarget RegionTarget = SelectionRegionTarget::Volume;
    SelectionRegionCriterion RegionCriterion =
        SelectionRegionCriterion::Geometry;
    SelectionPlanarConnectivity PlanarConnectivity =
        SelectionPlanarConnectivity::Four;
    SelectionVolumeConnectivity VolumeConnectivity =
        SelectionVolumeConnectivity::Six;
};

// --- Visibilite contextuelle : la regle, pas des if disperses --------------
//
//   cible Volume -> critere + voisinage volumique (6/26)
//   cible Face   -> critere + voisinage planaire (4/8)
//   cible Color  -> rien d'autre : le critere EST la couleur, et la
//                   propagation est globale, donc sans voisinage.
[[nodiscard]] constexpr bool RegionShowsCriterion(
    const SelectionRegionTarget target) noexcept
{
    return target != SelectionRegionTarget::Color;
}
[[nodiscard]] constexpr bool RegionShowsPlanarConnectivity(
    const SelectionRegionTarget target) noexcept
{
    return target == SelectionRegionTarget::Face;
}
[[nodiscard]] constexpr bool RegionShowsVolumeConnectivity(
    const SelectionRegionTarget target) noexcept
{
    return target == SelectionRegionTarget::Volume;
}

// --- Clic dans le vide : la semantique par operation -----------------------
//
// L'operation est celle capturee au PointerDown — jamais relue au relachement.
//   Replace   + vide -> selection vide (l'utilisateur a designe « rien »)
//   Add       + vide -> aucun changement (rien a ajouter)
//   Subtract  + vide -> aucun changement (rien a retirer)
//   Intersect + vide -> selection vide (intersection avec l'ensemble vide)
[[nodiscard]] constexpr bool EmptyClickClearsSelection(
    const SelectionMode mode) noexcept
{
    return mode == SelectionMode::Replace ||
        mode == SelectionMode::Intersect;
}

// --- Region : un outil de clic, jamais de marquee ---------------------------
//
// Region selectionne par propagation depuis le voxel clique. Un drag en mode
// Region ne demarre pas le marquee, et ne peut donc JAMAIS aboutir a un commit
// de boite. Le predicat est partage par le demarrage du geste et par la garde
// du commit : une seule regle, deux sites.
[[nodiscard]] constexpr bool SelectionModeStartsMarquee(
    const SelectionFamilyMode mode) noexcept
{
    return mode != SelectionFamilyMode::Region;
}

// --- Interactions de boite (poignees, deplacement) -------------------------
//
// Box et Rect editent une BOITE : poignees de redimensionnement et deplacement
// par l'interieur leur appartiennent. Region est un outil de CLIC : un clic
// dans une selection existante est destine au resolveur — surtout pour
// Subtract et Intersect, dont la cible naturelle EST la selection. La
// selection reste visible ; la deplacer passe par la famille Transform.
[[nodiscard]] constexpr bool SelectionModeAllowsEditableBoxInteraction(
    const SelectionFamilyMode mode) noexcept
{
    return mode != SelectionFamilyMode::Region;
}

// --- Regle Rect : une couche, sans contamination ---------------------------
//
// Meme principe que Geometry : l'etat precedent ne contamine pas le resultat.
// La boite du marquee est aplatie sur la couche d'ANCRAGE, le long de l'axe
// dominant de la normale visee au PointerDown. Fonction pure : le viewport
// l'applique a la preview ET au commit, qui ne peuvent donc pas diverger.
[[nodiscard]] constexpr int DominantAxisOf(
    const float x, const float y, const float z) noexcept
{
    const float ax = x < 0.0F ? -x : x;
    const float ay = y < 0.0F ? -y : y;
    const float az = z < 0.0F ? -z : z;
    return ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2;
}

[[nodiscard]] constexpr SelectionBounds FlattenSelectionBoundsToLayer(
    SelectionBounds bounds, const int axis, const std::int32_t layer) noexcept
{
    if (!bounds.Valid) return bounds;
    switch (axis)
    {
    case 0: bounds.Minimum.X = layer; bounds.Maximum.X = layer; break;
    case 2: bounds.Minimum.Z = layer; bounds.Maximum.Z = layer; break;
    default: bounds.Minimum.Y = layer; bounds.Maximum.Y = layer; break;
    }
    return bounds;
}

// PREVIEW == COMMIT pour le geste de creation. Cette fonction est l'UNIQUE
// regle : la preview (highlights) et le commit (relachement) l'appellent avec
// les memes entrees, et ne peuvent donc pas diverger. Box passe inchange ;
// Rect est aplati sur l'axe et la couche captures au PointerDown. Elle ne
// s'applique qu'au geste de CREATION â€” redimensionner ou deplacer une boite
// existante n'est pas un geste Rect.
[[nodiscard]] constexpr SelectionBounds ResolveSelectionGestureBounds(
    const SelectionFamilyMode mode, const SelectionBounds bounds,
    const int rectAxis, const std::int32_t rectLayer) noexcept
{
    return mode == SelectionFamilyMode::Rect
        ? FlattenSelectionBoundsToLayer(bounds, rectAxis, rectLayer)
        : bounds;
}

} // namespace VoxelForge::Editor
