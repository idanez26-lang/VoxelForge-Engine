#pragma once

#include "Selection/SelectionService.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace VoxelForge::Editor
{

enum class TransformPreviewVoxelState : std::uint8_t
{
    Valid,
    Collision,
    OutOfBounds
};

enum class TransformPreviewCollisionPolicy : std::uint8_t
{
    IgnoreSource,
    IncludeSource
};

struct TransformPreviewVoxel final
{
    Asset::Voxel::VoxelPosition SourcePosition{};
    Asset::Voxel::VoxelPosition PreviewPosition{};
    Asset::Voxel::Voxel Value{};
    TransformPreviewVoxelState State = TransformPreviewVoxelState::Valid;

    [[nodiscard]] bool operator==(
        const TransformPreviewVoxel&) const noexcept = default;
};

struct TransformPreviewDestinationVoxel final
{
    Asset::Voxel::VoxelPosition SourcePosition{};
    Asset::Voxel::VoxelPosition DestinationPosition{};
    Asset::Voxel::Voxel Value{};

    [[nodiscard]] bool operator==(
        const TransformPreviewDestinationVoxel&) const noexcept = default;
};

struct TransformPreviewRenderPlan final
{
    bool DrawIndividualVoxels = false;
    bool DrawIndividualCollisions = false;
    std::size_t SourceVoxelCount = 0U;
    std::size_t DestinationVoxelCount = 0U;
    std::size_t CollisionVoxelCount = 0U;
    std::size_t OutOfBoundsVoxelCount = 0U;
};

class TransformPreviewRenderPolicy final
{
public:
    static constexpr std::size_t IndividualVoxelLimit = 512U;
    static constexpr std::size_t IndividualCollisionLimit = 256U;

    [[nodiscard]] static TransformPreviewRenderPlan Build(
        std::size_t sourceVoxelCount,
        std::size_t destinationVoxelCount,
        std::size_t collisionCount,
        std::size_t outOfBoundsCount) noexcept;
};

// VF-WRAP-V1 (correctif, point 6) : resume COMPACT d'une preview dont le
// resultat est trop massif pour etre materialise a chaque delta. Le modele ne
// detient alors aucun voxel de destination : seulement des comptes et des
// bornes, calcules par l'autorite geometrique de l'outil. Le rendu se degrade
// en bornes/silhouette/validite ; le commit exige une materialisation
// (SetGeneratedVoxelDestinations) — jamais un commit depuis un resume.
struct TransformPreviewCompactSummary final
{
    std::size_t DestinationCount = 0U;
    SelectionBounds PreviewBounds{};        // bornes serrees du resultat occupe
    std::size_t CollisionCount = 0U;
    SelectionBounds CollisionBounds{};
    std::size_t OutOfBoundsCount = 0U;
    SelectionBounds OutOfBoundsBounds{};

    [[nodiscard]] bool operator==(
        const TransformPreviewCompactSummary&) const noexcept = default;
};

// Producteur de destinations : appele une fois, il pousse chaque destination
// dans le puits. Permet de materialiser un resultat massif directement dans
// le tampon du modele, sans vecteur intermediaire.
using TransformPreviewDestinationSink =
    std::function<void(const TransformPreviewDestinationVoxel&)>;
using TransformPreviewDestinationProducer =
    std::function<void(const TransformPreviewDestinationSink&)>;

struct TransformPreviewBufferMetrics final
{
    std::size_t CapturedCapacity = 0U;
    std::size_t SourcePositionCapacity = 0U;
    std::size_t ExplicitDestinationCapacity = 0U;
    std::size_t CollisionCapacity = 0U;
    std::size_t OutOfBoundsCapacity = 0U;
    std::uint64_t RebuildCount = 0U;
};

struct TransformPreviewRenderData final
{
    std::uint64_t Revision = 0U;
    bool DrawSourceGhost = true;
    std::span<const TransformPreviewVoxel> Voxels;
    std::span<const Asset::Voxel::VoxelPosition> SourcePositions;
    std::span<const Asset::Voxel::VoxelColor> Palette;
    SelectionBounds SourceBounds{};
    SelectionBounds PreviewBounds{};
    SelectionBounds CollisionBounds{};
    SelectionBounds OutOfBoundsBounds{};
    TransformPreviewRenderPlan Plan{};
};

// Read-only hand-off for a future atomic transform operation. It deliberately
// exposes no Apply method and owns none of the referenced document data.
struct TransformPreviewOperationData final
{
    std::uint64_t DocumentGeneration = 0U;
    std::uint64_t DocumentRevision = 0U;
    std::size_t ModelIndex = 0U;
    Asset::Voxel::VoxelPosition Delta{};
    SelectionBounds SourceBounds{};
    SelectionBounds PreviewBounds{};
    std::span<const TransformPreviewVoxel> Voxels;
    std::span<const TransformPreviewVoxel> SourceVoxels;
    std::span<const Asset::Voxel::VoxelPosition> SourcePositions;
    std::span<const Asset::Voxel::VoxelPosition> CollisionPositions;
    std::span<const Asset::Voxel::VoxelPosition> OutOfBoundsPositions;
};

class TransformPreviewModel final
{
public:
    [[nodiscard]] bool BeginPreview(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::size_t modelIndex = 0U,
        TransformPreviewCollisionPolicy collisionPolicy =
            TransformPreviewCollisionPolicy::IgnoreSource);
    [[nodiscard]] bool SetDelta(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        Asset::Voxel::VoxelPosition delta);
    [[nodiscard]] bool SetExplicitDestinations(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::span<const Asset::Voxel::VoxelPosition> destinations);
    [[nodiscard]] bool SetExplicitVoxelDestinations(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::span<const TransformPreviewDestinationVoxel> destinations);
    // Materialise `expectedCount` destinations produites par `produce` dans le
    // tampon du modele (reserve une fois). Toute destination doit referencer
    // un voxel source capture avec sa valeur ; sinon le modele revient a la
    // preview identite et retourne false. Toujours reconstruit (pas de
    // court-circuit "inchange") : c'est le chemin du commit.
    [[nodiscard]] bool SetGeneratedVoxelDestinations(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        std::size_t expectedCount,
        const TransformPreviewDestinationProducer& produce);
    // Passe en mode compact : aucun voxel materialise, comptes et bornes
    // fournis par l'outil. Retourne false si invalide ou si le resume est
    // identique au precedent (rien a redessiner).
    [[nodiscard]] bool SetCompactDestinations(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        const TransformPreviewCompactSummary& summary);
    [[nodiscard]] bool IsValidFor(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration) const noexcept;
    [[nodiscard]] bool CancelPreview() noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool HasCollisions() const noexcept;
    [[nodiscard]] bool HasOutOfBounds() const noexcept;
    [[nodiscard]] std::size_t VoxelCount() const noexcept;
    [[nodiscard]] std::size_t CollisionCount() const noexcept;
    [[nodiscard]] std::size_t OutOfBoundsCount() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;
    [[nodiscard]] std::uint64_t DocumentRevision() const noexcept;
    [[nodiscard]] std::size_t ModelIndex() const noexcept;
    [[nodiscard]] Asset::Voxel::VoxelPosition Delta() const noexcept;
    [[nodiscard]] bool HasExplicitDestinations() const noexcept;
    [[nodiscard]] bool HasExpandedDestinations() const noexcept;
    [[nodiscard]] bool IsCompact() const noexcept;
    [[nodiscard]] const SelectionBounds& SourceBounds() const noexcept;
    [[nodiscard]] const SelectionBounds& PreviewBounds() const noexcept;
    [[nodiscard]] std::span<const TransformPreviewVoxel> Voxels() const noexcept;
    [[nodiscard]] std::span<const TransformPreviewVoxel>
        SourceVoxels() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        SourcePositions() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        CollisionPositions() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        OutOfBoundsPositions() const noexcept;
    [[nodiscard]] TransformPreviewRenderData RenderData() const noexcept;
    [[nodiscard]] TransformPreviewOperationData OperationData() const noexcept;
    [[nodiscard]] TransformPreviewBufferMetrics Metrics() const noexcept;

private:
    [[nodiscard]] bool Rebuild(
        const Asset::Voxel::VoxelDocument& document);
    void ClearState() noexcept;

    std::vector<TransformPreviewVoxel> voxels_;
    std::vector<TransformPreviewVoxel> sourceVoxels_;
    std::vector<Asset::Voxel::VoxelPosition> sourcePositions_;
    std::vector<Asset::Voxel::VoxelPosition> explicitDestinations_;
    std::vector<Asset::Voxel::VoxelPosition> collisionPositions_;
    std::vector<Asset::Voxel::VoxelPosition> outOfBoundsPositions_;
    std::array<Asset::Voxel::VoxelColor, 256U> palette_{};
    SelectionBounds sourceBounds_{};
    SelectionBounds previewBounds_{};
    SelectionBounds collisionBounds_{};
    SelectionBounds outOfBoundsBounds_{};
    Asset::Voxel::VoxelDimensions dimensions_{};
    Asset::Voxel::VoxelPosition delta_{};
    std::filesystem::path sourcePath_;
    std::uint64_t documentGeneration_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    std::uint64_t renderRevision_ = 0U;
    std::uint64_t rebuildCount_ = 0U;
    std::size_t modelIndex_ = 0U;
    TransformPreviewCollisionPolicy collisionPolicy_ =
        TransformPreviewCollisionPolicy::IgnoreSource;
    TransformPreviewCompactSummary compactSummary_{};
    bool compact_ = false;
    bool expandedDestinations_ = false;
    bool active_ = false;
};

} // namespace VoxelForge::Editor
