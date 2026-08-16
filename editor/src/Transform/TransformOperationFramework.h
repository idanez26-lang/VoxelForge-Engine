#pragma once

#include "TransformPreviewModel.h"
#include "VoxelHistory/VoxelEditOperation.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace VoxelForge::Editor
{

enum class TransformSourcePolicy : std::uint8_t
{
    RemoveSource,
    PreserveSource
};

enum class TransformCollisionPolicy : std::uint8_t
{
    // Une destination peut recouvrir une SOURCE liberee ; toute autre cellule
    // occupee refuse le commit (Mirror historique).
    AllowSourceOverlap,
    // Toute cellule occupee refuse le commit (Duplicate).
    RejectAnyOccupiedDestination,
    // Decision produit (Tony) — Move / Scale / Rotate / Wrap : une destination
    // peut recouvrir n'importe quel voxel existant ; le commit FUSIONNE, le
    // voxel transforme est prioritaire (valeur), l'existant est remplace et
    // restaure exactement par Undo. Les destinations qui coincident (plusieurs
    // voxels transformes vers une meme cellule) sont dedupliquees : la
    // derniere emise l'emporte, deterministe. Les collisions detectees par la
    // preview restent une INFORMATION, jamais une invalidite.
    MergeOverlap
};

enum class TransformOperationBuildCode : std::uint8_t
{
    Ready,
    NoChange,
    InvalidPreview,
    InvalidDestinations,
    ModelChanged,
    SelectionChanged,
    Collision,
    OutOfBounds,
    Failed
};

struct TransformOperationPolicy final
{
    TransformSourcePolicy Source = TransformSourcePolicy::RemoveSource;
    TransformCollisionPolicy Collision =
        TransformCollisionPolicy::AllowSourceOverlap;
};

struct TransformOperationRequest final
{
    std::string_view Name;
    std::string Label;
    TransformOperationPolicy Policy{};
    // Bornes SERREES du resultat occupe : validees contre les destinations.
    SelectionBounds DestinationBounds{};
    // VF-WRAP-V1 (correctif) : bornes SEMANTIQUES (editables) enregistrees
    // dans la transition d'historique, distinctes des bornes serrees.
    // Invalides (defaut) : comportement historique — Before/After portent
    // les bornes serrees de la source et des destinations, exactement comme
    // avant, pour toutes les Transform existantes. Valides : Before/After
    // portent ces bornes, qui doivent CONTENIR les bornes serrees
    // correspondantes ; Undo restaure les bornes source, Redo les bornes
    // demandees. Aucun patch apres Redo n'est necessaire.
    SelectionBounds SourceEditableBounds{};
    SelectionBounds DestinationEditableBounds{};
};

struct TransformOperationBuildResult final
{
    TransformOperationBuildCode Code = TransformOperationBuildCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == TransformOperationBuildCode::Ready;
    }
};

// Validates an immutable transform preview and builds one atomic history
// operation. Geometry remains the responsibility of each concrete tool.
class TransformOperationBuilder final
{
public:
    [[nodiscard]] static TransformOperationBuildResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewModel& preview,
        TransformOperationRequest request);
};

[[nodiscard]] const char* TransformOperationBuildCodeName(
    TransformOperationBuildCode code) noexcept;

} // namespace VoxelForge::Editor
