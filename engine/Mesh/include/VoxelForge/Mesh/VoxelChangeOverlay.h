#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>

namespace VoxelForge::Mesh
{

// VF-0265 (lot 2) : vue « document + changements en attente », en lecture seule.
//
// Elle emprunte le document, n'en possède rien, ne le mute pas et ne journalise
// rien. Sa mémoire est proportionnelle au nombre de CHANGEMENTS, pas à la taille
// du document : c'est précisément ce qui supprime la copie intégrale que faisait
// le compositeur de preview (une allocation par voxel, 1 354 ms à un million).
//
// Elle répond aussi pour les positions HORS d'une région donnée, ce qui rend la
// visibilité des faces exacte aux frontières : le constructeur de mesh consulte
// les six voisins d'un voxel de bord, et il obtient l'état final de chacun.
//
// Les règles de rejet sont celles de VoxelDocument::ValidateVoxelChanges — une
// seule implémentation. Un jeu de changements refusé ici est refusé au commit,
// avec le même code d'erreur et le même message.
class VoxelChangeOverlay final
{
public:
    [[nodiscard]] static std::optional<VoxelChangeOverlay> TryCreate(
        const Asset::Voxel::VoxelDocument& document,
        std::span<const Asset::Voxel::VoxelDocumentChange> changes,
        std::size_t modelIndex,
        Asset::Voxel::VoxelDocumentOperationResult& validation);

    [[nodiscard]] bool HasVoxel(
        const Asset::Voxel::VoxelPosition& position) const noexcept;
    [[nodiscard]] std::optional<Asset::Voxel::Voxel> GetVoxel(
        const Asset::Voxel::VoxelPosition& position) const noexcept;

    /// Union des bornes du sous-modèle et de la boîte des positions AJOUTÉES.
    /// Indispensable : le constructeur de mesh clippe la région demandée à ces
    /// bornes, et un voxel créé hors des bornes courantes serait sinon perdu.
    [[nodiscard]] const Asset::Voxel::VoxelBounds& Bounds() const noexcept;

    /// Boîte des positions changées, dilatée d'un voxel sur chaque axe. C'est
    /// la seule zone dont les faces peuvent différer : au-delà, ni le voxel ni
    /// aucun de ses six voisins n'a changé.
    [[nodiscard]] const Asset::Voxel::VoxelBounds& DirtyBounds() const noexcept;

    [[nodiscard]] std::uint64_t VoxelCount() const noexcept;
    /// Vrai quand aucun changement n'est en attente : la vue est alors le
    /// document lui-même.
    [[nodiscard]] bool Empty() const noexcept;

    void ForEachVoxel(
        const std::function<void(
            const Asset::Voxel::VoxelPosition&,
            const Asset::Voxel::Voxel&)>& visitor) const;

private:
    VoxelChangeOverlay() = default;

    using Overrides = std::unordered_map<
        Asset::Voxel::VoxelPosition,
        std::optional<Asset::Voxel::Voxel>,
        Asset::Voxel::VoxelPositionHash>;

    const Asset::Voxel::VoxelSubModel* model_ = nullptr;
    Overrides overrides_;
    Asset::Voxel::VoxelBounds bounds_{};
    Asset::Voxel::VoxelBounds dirtyBounds_{};
    std::uint64_t voxelCount_ = 0U;
};

} // namespace VoxelForge::Mesh
