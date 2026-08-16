#pragma once

// VF-WRAP-V1 — Transform Wrap : Repeat / Crop du motif de la selection.
//
// SEMANTIQUE PRODUIT (validee par Tony) : le volume contenu dans les
// EditableBounds source constitue le MOTIF, vide interieur compris. Tirer une
// face des bounds repete periodiquement le motif (agrandissement) ou le crope
// (reduction) ; la face opposee reste l'ANCRAGE. Le spacing insere du vide
// entre les repetitions ; Mirror Repeat alterne l'orientation des repetitions
// successives — ce n'est PAS le Transform Mirror historique, qui reste intact.
//
// Ce fichier est la SEULE autorite geometrique de Wrap. Preview et commit
// consomment la meme application `LocalPatternCoordinate` a travers le meme
// enumerateur `ForEachDestination` : PREVIEW == COMMIT par construction, que
// la preview soit materialisee (petit resultat) ou compacte (resultat massif,
// point 6 du correctif). Aucune logique Wrap dans le renderer.
//
// COUT : l'enumeration est proportionnelle au nombre de DESTINATIONS
// produites (plus l'etendue des axes), jamais au volume des bounds ni au
// document. Le resume (compte, bornes serrees) est analytique : O(sources +
// etendues). Les collisions en mode compact sont suivies incrementalement sur
// la difference de bounds entre deux deltas de drag.

#include "TransformOperationFramework.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace VoxelForge::Editor
{

// Face fixe du geste sur un axe. Tirer X+ ancre au minimum ; tirer X- ancre au
// maximum. Sur un axe dont l'etendue ne change pas, l'ancre est sans effet.
enum class WrapAxisAnchor : std::uint8_t
{
    Minimum,
    Maximum
};

struct WrapAxisOptions final
{
    std::int32_t Spacing = 0;      // vide insere entre repetitions, >= 0
    bool MirrorRepeat = false;     // alterne l'orientation des repetitions
    WrapAxisAnchor Anchor = WrapAxisAnchor::Minimum;

    [[nodiscard]] bool operator==(const WrapAxisOptions&) const noexcept
        = default;
};

struct VoxelWrapOptions final
{
    WrapAxisOptions X{};
    WrapAxisOptions Y{};
    WrapAxisOptions Z{};

    [[nodiscard]] bool operator==(const VoxelWrapOptions&) const noexcept
        = default;
};

// Index du motif : position source absolue -> voxel source. Construit une
// fois par geste depuis la capture du modele de preview (copie independante :
// aucune reference pendante si le modele est reinitialise). Les cles sont
// exactement les positions source ; `Find(p) != nullptr` <=> p est une source.
class WrapPatternIndex final
{
public:
    WrapPatternIndex() = default;
    WrapPatternIndex(
        std::span<const TransformPreviewVoxel> sourceVoxels,
        SelectionBounds sourceBounds);

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] const SelectionBounds& SourceBounds() const noexcept;
    [[nodiscard]] std::span<const TransformPreviewVoxel> Sources() const noexcept;
    [[nodiscard]] const TransformPreviewVoxel* Find(
        Asset::Voxel::VoxelPosition position) const noexcept;
    void Reset() noexcept;

private:
    struct PositionHash final
    {
        std::size_t operator()(Asset::Voxel::VoxelPosition p) const noexcept;
    };
    std::vector<TransformPreviewVoxel> sources_;
    std::unordered_map<Asset::Voxel::VoxelPosition, std::size_t, PositionHash>
        lookup_;
    SelectionBounds sourceBounds_{};
};

// Resume analytique du resultat : ce que la preview compacte affiche et ce
// que le commit produira. Bounds = bornes SERREES du resultat occupe ;
// RequestedBounds = bornes demandees par le geste (autorite des guides).
struct VoxelWrapSummary final
{
    std::uint64_t DestinationCount = 0U;
    SelectionBounds Bounds{};
    SelectionBounds RequestedBounds{};
    std::string Message;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Message.empty() && DestinationCount > 0U && Bounds.Valid;
    }
};

struct VoxelWrapGeometry final
{
    std::vector<TransformPreviewDestinationVoxel> Destinations;
    // Bornes SERREES du resultat occupe — exigees par le framework de commit.
    SelectionBounds Bounds{};
    // Bornes demandees par le geste (peuvent depasser Bounds quand le crop
    // traverse une zone vide du motif).
    SelectionBounds RequestedBounds{};
    std::string Message;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Message.empty() && !Destinations.empty() && Bounds.Valid;
    }
};

// Etat des collisions d'une preview compacte : compte EXACT (meme regle que
// le modele : destination occupee par un voxel du document qui n'est pas une
// source), bornes exactes tant que les positions tiennent dans
// `PositionLimit`, bornes conservatrices (cible ∩ document) au-dela.
struct WrapCollisionState final
{
    std::size_t Count = 0U;
    SelectionBounds Bounds{};
    bool PositionsTracked = true;
};

// Suivi incremental des collisions pendant un drag compact. Entre deux deltas
// dont seule la cible change (meme motif, memes options, meme document), il
// ne visite que les destinations de la DIFFERENCE de bounds ; sinon il
// recalcule en visitant le moins couteux : les destinations de la cible ou
// les voxels du document (jamais les deux, jamais le volume).
class WrapCollisionTracker final
{
public:
    static constexpr std::size_t PositionLimit = 65'536U;

    void Reset() noexcept;
    [[nodiscard]] WrapCollisionState Update(
        const Asset::Voxel::VoxelDocument& document,
        std::size_t modelIndex,
        const WrapPatternIndex& pattern,
        SelectionBounds target,
        const VoxelWrapOptions& options,
        std::uint64_t destinationCount);
    [[nodiscard]] const WrapCollisionState& State() const noexcept;
    // Instrumentation : destinations visitees par la derniere mise a jour.
    [[nodiscard]] std::uint64_t LastVisited() const noexcept;

private:
    struct Signature final
    {
        SelectionBounds SourceBounds{};
        std::size_t SourceCount = 0U;
        VoxelWrapOptions Options{};
        std::uint64_t Revision = 0U;
        std::size_t ModelIndex = 0U;
        [[nodiscard]] bool operator==(const Signature&) const noexcept
            = default;
    };
    void Recompute(
        const Asset::Voxel::VoxelDocument& document, std::size_t modelIndex,
        const WrapPatternIndex& pattern, SelectionBounds target,
        const VoxelWrapOptions& options, std::uint64_t destinationCount);
    void Scan(
        const Asset::Voxel::VoxelDocument& document, std::size_t modelIndex,
        const WrapPatternIndex& pattern, SelectionBounds box,
        const VoxelWrapOptions& options, bool add);
    void RefreshBounds(
        const Asset::Voxel::VoxelDocument& document, std::size_t modelIndex,
        SelectionBounds target) noexcept;

    Signature signature_{};
    SelectionBounds previousTarget_{};
    std::vector<Asset::Voxel::VoxelPosition> positions_;
    WrapCollisionState state_{};
    std::uint64_t lastVisited_ = 0U;
    bool primed_ = false;
};

enum class WrapVoxelSelectionResultCode : std::uint8_t
{
    Ready,
    NoChange,
    InvalidGeometry,
    InvalidPreview,
    ModelChanged,
    SelectionChanged,
    Collision,
    OutOfBounds,
    Failed
};

// Deplacable, JAMAIS copiable : l'operation (millions de VoxelChange a grande
// echelle) est transferee a l'historique par std::move, comme pour les autres
// Transform. Une copie profonde accidentelle est une erreur de compilation.
struct WrapVoxelSelectionResult final
{
    WrapVoxelSelectionResultCode Code = WrapVoxelSelectionResultCode::Failed;
    VoxelEditOperation Operation;
    std::string Message;

    WrapVoxelSelectionResult() = default;
    WrapVoxelSelectionResult(WrapVoxelSelectionResultCode code,
        VoxelEditOperation operation, std::string message)
        : Code(code), Operation(std::move(operation)),
          Message(std::move(message)) {}
    WrapVoxelSelectionResult(const WrapVoxelSelectionResult&) = delete;
    WrapVoxelSelectionResult& operator=(const WrapVoxelSelectionResult&) = delete;
    WrapVoxelSelectionResult(WrapVoxelSelectionResult&&) noexcept = default;
    WrapVoxelSelectionResult& operator=(WrapVoxelSelectionResult&&) noexcept
        = default;

    [[nodiscard]] bool Ready() const noexcept
    {
        return Code == WrapVoxelSelectionResultCode::Ready;
    }
};

class WrapVoxelSelectionOperation final
{
public:
    // Budget de materialisation EXACTE pendant le drag : au-dela de ce nombre
    // de destinations, la preview passe en resume compact (bornes /
    // silhouette / validite), et la materialisation n'a lieu qu'au commit.
    // Mesure : 32^3 destinations denses = ~3 ms par delta en RelWithDebInfo ;
    // 64^3 = ~22 ms, au-dela d'une frame a 60 FPS.
    static constexpr std::size_t ExactPreviewDestinationBudget = 32'768U;

    // Coordonnee absolue du motif pour une coordonnee de destination sur UN
    // axe ; -1 pour une cellule hors motif (spacing, hors domaine de l'ancre).
    [[nodiscard]] static std::int32_t LocalPatternCoordinate(
        std::int32_t destination, std::int32_t sourceMinimum,
        std::int32_t sourceMaximum, const WrapAxisOptions& axis) noexcept;

    // Resume analytique : compte exact et bornes serrees, sans materialiser.
    [[nodiscard]] static VoxelWrapSummary Summarize(
        const WrapPatternIndex& pattern,
        SelectionBounds newBounds,
        const VoxelWrapOptions& options);

    // Enumere chaque destination (source, position, valeur) du resultat dans
    // `newBounds`. Cout proportionnel aux destinations produites.
    static void ForEachDestination(
        const WrapPatternIndex& pattern,
        SelectionBounds newBounds,
        const VoxelWrapOptions& options,
        const std::function<void(const TransformPreviewDestinationVoxel&)>&
            sink);

    // Materialisation complete en vecteur (tests, petite preview exacte).
    [[nodiscard]] static VoxelWrapGeometry BuildGeometry(
        const WrapPatternIndex& pattern,
        SelectionBounds newBounds,
        const VoxelWrapOptions& options);
    [[nodiscard]] static VoxelWrapGeometry BuildGeometry(
        std::span<const TransformPreviewVoxel> sourceVoxels,
        SelectionBounds sourceBounds,
        SelectionBounds newBounds,
        const VoxelWrapOptions& options);

    // Borne superieure de la memoire que l'operation de commit occupera dans
    // l'historique (changements + transition), pour refuser AVANT toute
    // materialisation un resultat que l'historique refuserait de toute facon.
    [[nodiscard]] static std::size_t EstimateOperationMemory(
        std::uint64_t destinationCount, std::size_t sourceCount) noexcept;

    // Commit : re-derive le resultat depuis les MEMES bornes source (les
    // EditableBounds du debut du geste, passees explicitement), le materialise
    // dans le modele de preview par le producteur (une seule copie), puis
    // delegue validation et operation atomique au framework commun. La
    // transition d'historique porte les bornes editables source (Before) et
    // demandees (After).
    [[nodiscard]] static WrapVoxelSelectionResult Build(
        const Asset::Voxel::VoxelDocument& document,
        const SelectionService& selection,
        std::uint64_t documentGeneration,
        TransformPreviewModel& preview,
        SelectionBounds sourceBounds,
        SelectionBounds newBounds,
        const VoxelWrapOptions& options);
};

} // namespace VoxelForge::Editor
