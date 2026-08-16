#pragma once

// VF-UX-SELECTION-V1 — resolveur Region.
//
// Architecture demandee : requete -> resolveur -> operation sur le
// SelectionSet. Le resolveur ne connait ni le renderer ni l'editeur : il lit
// le document a travers deux fonctions, et rend une liste de positions que
// SelectionService::Apply consomme telle quelle.
//
// Performance : propagation par graine sur les voxels OCCUPES uniquement —
// O(taille de la region), jamais O(volume). La cible Color enumere les voxels
// existants du document (stockage creux), pas la grille.

#include "Selection/SelectionUxModes.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace VoxelForge::Editor
{

struct SelectionRegionRequest final
{
    Asset::Voxel::VoxelPosition Seed{};
    // Normale de la face visee au clic ; requise pour la cible Face, ignoree
    // sinon. L'axe dominant definit le plan de propagation.
    float NormalX = 0.0F;
    float NormalY = 1.0F;
    float NormalZ = 0.0F;

    SelectionRegionTarget Target = SelectionRegionTarget::Volume;
    SelectionRegionCriterion Criterion = SelectionRegionCriterion::Geometry;
    SelectionPlanarConnectivity Planar = SelectionPlanarConnectivity::Four;
    SelectionVolumeConnectivity Volume = SelectionVolumeConnectivity::Six;

    // Index de palette du voxel s'il existe, nullopt sinon.
    std::function<std::optional<std::uint8_t>(Asset::Voxel::VoxelPosition)>
        ReadVoxel;
    // Enumeration des voxels EXISTANTS (cible Color uniquement).
    std::function<void(const std::function<void(
        Asset::Voxel::VoxelPosition, std::uint8_t)>&)> ForEachVoxel;

    // Garde-fou : borne la propagation, tres au-dessus de tout cas reel
    // (256^3 = 16,7 M). Jamais atteinte en usage normal.
    std::size_t MaximumCells = 20'000'000U;
};

struct SelectionRegionResult final
{
    // Triees et dedupliquees : directement consommables par
    // SelectionService::Apply.
    std::vector<Asset::Voxel::VoxelPosition> Positions;
    bool TruncatedByLimit = false;

    [[nodiscard]] bool Empty() const noexcept { return Positions.empty(); }
};

[[nodiscard]] SelectionRegionResult ResolveSelectionRegion(
    const SelectionRegionRequest& request);

} // namespace VoxelForge::Editor
