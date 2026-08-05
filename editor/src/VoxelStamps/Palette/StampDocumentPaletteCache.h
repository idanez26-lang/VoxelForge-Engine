#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace VoxelForge::Editor::Stamps
{

/// Indices de palette réellement occupés par un document, sur les 256
/// possibles. C'est exactement ce que `StampPlacementPlanner::Build` relevait
/// en parcourant tout le document à chaque reconstruction de plan : mesuré à
/// 68 ms sur un million de voxels (VF-0264), pour un résultat qui ne change
/// qu'avec la révision du document.
using StampOccupiedPaletteIndices = std::array<bool, 256U>;

/// Cache d'une seule entrée, détenu par la session de placement. Il ne prend
/// aucune décision métier : il mémorise le résultat du même parcours de
/// référence et le rend tant que le document n'a pas changé.
///
/// Clé d'invalidation : identité d'instance du document + révision. Le
/// sous-modèle n'en fait volontairement PAS partie — le relevé est
/// document-entier, comme le parcours d'origine qui visitait tous les
/// sous-modèles. L'y ajouter provoquerait des recalculs inutiles à chaque
/// changement de cible.
///
/// L'identité d'instance seule ne suffirait pas : une adresse réutilisée par un
/// autre document validerait un relevé périmé. La révision, qui repart à zéro
/// pour tout nouveau document, ferme cette porte.
class StampDocumentPaletteCache final
{
public:
    /// Relevé pour ce document. Recalcule uniquement si la clé a changé.
    [[nodiscard]] const StampOccupiedPaletteIndices& Resolve(
        const Asset::Voxel::VoxelDocument& document);

    void Clear() noexcept;

    /// Compteurs de diagnostic : ils rendent la réutilisation observable par
    /// les tests, sans chronomètre.
    [[nodiscard]] std::uint64_t HitCount() const noexcept;
    [[nodiscard]] std::uint64_t MissCount() const noexcept;

    /// Parcours de référence, sans cache. Le cache et les appelants sans cache
    /// passent par cette unique fonction : il n'existe pas deux vérités.
    [[nodiscard]] static StampOccupiedPaletteIndices Scan(
        const Asset::Voxel::VoxelDocument& document) noexcept;

private:
    StampOccupiedPaletteIndices indices_{};
    std::uintptr_t documentInstanceToken_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    bool valid_ = false;
    std::uint64_t hitCount_ = 0U;
    std::uint64_t missCount_ = 0U;
};

} // namespace VoxelForge::Editor::Stamps
