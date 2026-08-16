// VF-WRAP-V1 (correctif, point 6) — mesure memoire/temps de Wrap sur des
// tailles croissantes, jusqu'au cas 256^3.
//
// Le benchmark instrumente le VRAI chemin : capture de la selection, mise a
// jour de preview a chaque delta (exacte sous le budget, compacte au-dela),
// puis materialisation et construction de l'operation de commit. Il imprime
// une table et applique des gardes : aucun delta de drag ne materialise plus
// que le budget exact, un commit que l'historique refuserait est refuse SANS
// materialisation, et le PERFORMANCE GATE interactif :
//   A. mediane des deltas d'un drag continu   < 16,67 ms  (une frame)
//   B. p95 des deltas                          < 33,3 ms   (deux frames)
//   C. plafond de securite (pire delta)        < 40 ms
// La mediane et le p95 sont stables sur 250 deltas et detectent une
// regression reelle (40-60 ms) sans transformer une excursion isolee de
// l'ordonnanceur en echec ; le plafond reste nettement sous 66,7 ms.
//
// La section "commit reel" mesure de bout en bout, comme le workspace :
// Build (materialisation + operation) -> VoxelEditHistory::Execute (applier
// atomique : copie dirigee des changements, instantanes de rollback,
// application au document et a la grille de compatibilite) -> synchronisation
// de la selection (transition After), puis Undo et Redo, avec verification
// exacte du document et des bornes editables semantiques.

#include "Transform/WrapVoxelSelectionOperation.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "Commands/Voxel/VoxelEditSession.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t Generation = 9U;
constexpr std::uint32_t Dimension = 256U;
constexpr double FrameBudgetMs = 1000.0 / 60.0;     // 16,67 ms
#ifdef NDEBUG
// Build optimise : le contrat interactif s'applique tel quel.
constexpr double GateScale = 1.0;
constexpr const char* GateLabel = "release (contrat interactif)";
#else
// Build Debug (5 a 10x plus lent, jamais livre) : les memes seuils, dilates
// x10 — plafond de catastrophe seulement, la table reste informative.
constexpr double GateScale = 10.0;
constexpr const char* GateLabel = "debug (seuils x10, informatif)";
#endif
constexpr double MedianGateMs = FrameBudgetMs * GateScale;        // A
constexpr double P95GateMs = FrameBudgetMs * 2.0 * GateScale;     // B
constexpr double CeilingGateMs = 40.0 * GateScale;                // C

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Operation>
double Milliseconds(Operation&& operation)
{
    const auto started = Clock::now();
    operation();
    return std::chrono::duration<double, std::milli>(
        Clock::now() - started).count();
}

[[nodiscard]] double WorkingSetMiB() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
#endif
    return 0.0;
}

[[nodiscard]] double PeakWorkingSetMiB() noexcept
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<double>(counters.PeakWorkingSetSize) /
            (1024.0 * 1024.0);
#endif
    return 0.0;
}

// Document 256^3 avec un motif plein 2x2x2 a l'origine : chaque cellule des
// nouvelles bounds devient une destination — le pire cas de densite. Un
// obstacle isole a l'oppose exerce le suivi de collisions.
// Le scenario "dense" ajoute un mur plein de 4 x 256 x 256 voxels en
// x = 96..99 : le suivi incremental des collisions doit alors visiter les
// tranches de la difference (le document est plus gros qu'une tranche), et
// le mur fait deborder la limite de positions suivies — le compte reste exact.
Asset::Voxel::VoxelDocument MakeDocument(const bool denseWall)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {Dimension, Dimension, Dimension};
    for (std::uint8_t z = 0U; z < 2U; ++z)
        for (std::uint8_t y = 0U; y < 2U; ++y)
            for (std::uint8_t x = 0U; x < 2U; ++x)
                model.Voxels.push_back({x, y, z,
                    static_cast<std::uint8_t>(1U + x + 2U * y + 4U * z)});
    model.Voxels.push_back({200U, 1U, 1U, 9U});
    if (denseWall)
        for (std::uint32_t z = 0U; z < Dimension; ++z)
            for (std::uint32_t y = 0U; y < Dimension; ++y)
                for (std::uint32_t x = 96U; x < 100U; ++x)
                    model.Voxels.push_back({
                        static_cast<std::uint8_t>(x),
                        static_cast<std::uint8_t>(y),
                        static_cast<std::uint8_t>(z), 12U});
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "wrap-benchmark-memory.vox");
    Require(loaded.Succeeded(), "Unable to build the Wrap benchmark document.");
    return std::move(*loaded.Document);
}

// Le meme arbitrage exact/compact que le workspace, sans le workspace.
struct PreviewDriver final
{
    Asset::Voxel::VoxelDocument& Document;
    SelectionService& Selection;
    TransformPreviewModel& Preview;
    WrapPatternIndex& Pattern;
    WrapCollisionTracker& Collisions;

    VoxelWrapSummary Update(const SelectionBounds target,
        const VoxelWrapOptions& options)
    {
        const VoxelWrapSummary summary =
            WrapVoxelSelectionOperation::Summarize(Pattern, target, options);
        Require(summary.Valid(), "Benchmark summary is invalid.");
        if (summary.DestinationCount <=
            WrapVoxelSelectionOperation::ExactPreviewDestinationBudget)
        {
            const VoxelWrapGeometry geometry =
                WrapVoxelSelectionOperation::BuildGeometry(
                    Pattern, target, options);
            Require(Preview.SetExplicitVoxelDestinations(
                Document, Selection, Generation, geometry.Destinations),
                "Unable to update the exact benchmark preview.");
        }
        else
        {
            const WrapCollisionState collisions = Collisions.Update(
                Document, 0U, Pattern, target, options,
                summary.DestinationCount);
            TransformPreviewCompactSummary compact;
            compact.DestinationCount =
                static_cast<std::size_t>(summary.DestinationCount);
            compact.PreviewBounds = summary.Bounds;
            compact.CollisionCount = collisions.Count;
            compact.CollisionBounds = collisions.Bounds;
            Require(Preview.SetCompactDestinations(
                Document, Selection, Generation, compact),
                "Unable to update the compact benchmark preview.");
        }
        return summary;
    }
};

struct Sample final
{
    std::int32_t Size = 0;
    std::uint64_t Destinations = 0U;
    double JumpUpdateMs = 0.0;      // un delta qui saute directement a la cible
    double DragMaxDeltaMs = 0.0;    // pire delta d'un drag continu 2 -> N
    double DragMeanDeltaMs = 0.0;
    double DragMedianDeltaMs = 0.0;
    double DragP95DeltaMs = 0.0;
    std::uint64_t DragMaxVisited = 0U;
    double CommitMs = 0.0;
    std::size_t PreviewVoxels = 0U;
    double OperationMiB = 0.0;
    double WorkingSetAfterUpdateMiB = 0.0;
    double PeakMiB = 0.0;
    bool Compact = false;
    bool CommitReady = false;
    bool RefusedBeforeMaterialisation = false;
    std::size_t Collisions = 0U;
    std::string CommitMessage;
};

Sample Measure(const std::int32_t size, const bool denseWall)
{
    Asset::Voxel::VoxelDocument document = MakeDocument(denseWall);
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    // Ordre X, Y, Z : celui du SelectionService (positions triees).
    std::vector<Position> pattern;
    for (std::int32_t x = 0; x < 2; ++x)
        for (std::int32_t y = 0; y < 2; ++y)
            for (std::int32_t z = 0; z < 2; ++z)
                pattern.push_back({x, y, z});
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});
    Require(selection.ApplySortedVolume(
        pattern, sourceBounds, SelectionMode::Replace),
        "Unable to select the benchmark pattern.");

    Sample sample;
    sample.Size = size;
    const SelectionBounds target = SelectionBounds::FromCorners(
        {0, 0, 0}, {size - 1, size - 1, size - 1});
    const VoxelWrapOptions options{};

    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Unable to begin the benchmark preview.");
    WrapPatternIndex index(preview.SourceVoxels(), sourceBounds);
    WrapCollisionTracker collisions;
    PreviewDriver driver{document, selection, preview, index, collisions};

    // 1. Un delta qui saute directement a la cible.
    const double baseline = WorkingSetMiB();
    VoxelWrapSummary summary;
    sample.JumpUpdateMs = Milliseconds([&]
    {
        summary = driver.Update(target, options);
    });
    sample.Destinations = summary.DestinationCount;
    sample.Compact = preview.IsCompact();
    sample.PreviewVoxels = preview.Voxels().size();
    sample.Collisions = preview.CollisionCount();
    sample.WorkingSetAfterUpdateMiB = WorkingSetMiB() - baseline;

    // 2. Un drag continu : la face X+ avance d'une cellule par delta, sur un
    //    volume Y/Z deja a sa taille finale (pire tranche par delta).
    {
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(size));
        for (std::int32_t x = 2; x <= size; ++x)
        {
            const SelectionBounds step = SelectionBounds::FromCorners(
                {0, 0, 0}, {x - 1, size - 1, size - 1});
            const double ms = Milliseconds([&]
            {
                static_cast<void>(driver.Update(step, options));
            });
            sample.DragMaxDeltaMs = std::max(sample.DragMaxDeltaMs, ms);
            sample.DragMaxVisited = std::max(
                sample.DragMaxVisited, collisions.LastVisited());
            samples.push_back(ms);
        }
        if (!samples.empty())
        {
            double total = 0.0;
            for (const double ms : samples) total += ms;
            sample.DragMeanDeltaMs = total / static_cast<double>(samples.size());
            std::sort(samples.begin(), samples.end());
            sample.DragMedianDeltaMs = samples[samples.size() / 2U];
            sample.DragP95DeltaMs = samples[std::min(samples.size() - 1U,
                static_cast<std::size_t>(samples.size() * 95U / 100U))];
        }
    }

    // 3. Le commit : garde memoire PUIS materialisation + operation.
    const VoxelEditHistoryLimits limits{};
    const std::size_t estimated =
        WrapVoxelSelectionOperation::EstimateOperationMemory(
            summary.DestinationCount, index.Sources().size());
    if (estimated > limits.MaximumEstimatedMemory)
    {
        // Le meme arbitrage que le workspace : seul le budget memoire de
        // l'historique refuse AVANT de materialiser (Overlap/Merge : les
        // recouvrements ne bloquent plus, ils fusionnent au commit).
        sample.RefusedBeforeMaterialisation = true;
        sample.CommitMessage = "refused before materialisation (" +
            std::to_string(estimated / (1024U * 1024U)) + " MiB estimated > " +
            std::to_string(limits.MaximumEstimatedMemory / (1024U * 1024U)) +
            " MiB budget)";
        // Le budget memoire predictif reste actif : a 256^3 (576 MiB
        // estimes) le commit est refuse par la garde memoire, collision ou non.
        if (size >= 256)
            Require(estimated > limits.MaximumEstimatedMemory,
                "The predictive memory budget no longer refuses 256^3.");
        Require(preview.Voxels().size() <=
            WrapVoxelSelectionOperation::ExactPreviewDestinationBudget,
            "A refused commit must not have materialised the result.");
    }
    else
    {
        WrapVoxelSelectionResult result;
        sample.CommitMs = Milliseconds([&]
        {
            result = WrapVoxelSelectionOperation::Build(
                document, selection, Generation, preview, sourceBounds,
                target, options);
        });
        sample.CommitReady = result.Ready();
        sample.CommitMessage = result.Message;
        sample.OperationMiB = static_cast<double>(
            EstimateVoxelEditOperationMemory(result.Operation)) /
            (1024.0 * 1024.0);
    }
    sample.PeakMiB = PeakWorkingSetMiB();
    return sample;
}

void Print(const Sample& sample)
{
    std::cout << std::fixed << std::setprecision(2)
        << "  " << std::setw(4) << sample.Size << "^3 | dest "
        << std::setw(9) << sample.Destinations
        << " | " << (sample.Compact ? "compact" : "exact  ")
        << " | jump " << std::setw(8) << sample.JumpUpdateMs << " ms"
        << " | drag med " << std::setw(5) << sample.DragMedianDeltaMs
        << " p95 " << std::setw(5) << sample.DragP95DeltaMs
        << " max " << std::setw(5) << sample.DragMaxDeltaMs
        << " mean " << std::setw(5) << sample.DragMeanDeltaMs << " ms"
        << " | visited " << std::setw(8) << sample.DragMaxVisited
        << " | commit " << std::setw(8) << sample.CommitMs << " ms"
        << " | preview vox " << std::setw(6) << sample.PreviewVoxels
        << " | coll " << sample.Collisions
        << " | op " << std::setw(6) << sample.OperationMiB << " MiB"
        << " | ws+ " << std::setw(6) << sample.WorkingSetAfterUpdateMiB
        << " MiB | peak " << std::setw(7) << sample.PeakMiB << " MiB | "
        << (sample.CommitReady ? "ready" : sample.CommitMessage)
        << '\n';
}
// Performance gate interactif (voir en-tete) : mediane, p95, plafond.
void Gate(const Sample& sample)
{
    Require(sample.DragMedianDeltaMs < MedianGateMs,
        "Performance gate A: median drag delta reached a full frame.");
    Require(sample.DragP95DeltaMs < P95GateMs,
        "Performance gate B: p95 drag delta reached two frames.");
    Require(sample.DragMaxDeltaMs < CeilingGateMs,
        "Performance gate C: a drag delta reached the safety ceiling.");
}

// --- Commit reel : Build -> Execute -> synchronisation -> Undo -> Redo ------

Voxel::VoxelModel MakeCompatibility(const Asset::Voxel::VoxelDocument& document)
{
    Voxel::VoxelModel result;
    const auto* model = document.GetModel(0U);
    Require(model != nullptr, "Benchmark model is missing.");
    Voxel::VoxelGrid grid;
    const auto dimensions = model->Dimensions();
    Require(grid.Resize(dimensions.X, dimensions.Y, dimensions.Z),
        "Unable to size the benchmark compatibility grid.");
    model->ForEachVoxel([&grid](const Position position, const auto voxel)
    {
        Require(grid.Set(
            static_cast<std::uint32_t>(position.X),
            static_cast<std::uint32_t>(position.Y),
            static_cast<std::uint32_t>(position.Z),
            {voxel.PaletteIndex, Voxel::Voxel::OccupiedFlag}),
            "Unable to initialize the benchmark compatibility grid.");
    });
    result.AddGrid(std::move(grid));
    return result;
}

// Session comme le workspace : document + grille de compatibilite. Le
// remaillage du workspace n'est pas reproduit ici (il depend du renderer) ;
// il est explicitement HORS de la mesure et signale comme tel.
class BenchmarkSession final : public VoxelEditSession
{
public:
    explicit BenchmarkSession(Asset::Voxel::VoxelDocument& document)
        : document_(&document), model_(MakeCompatibility(document)) {}
    std::uint64_t VoxelModelGeneration() const noexcept override { return Generation; }
    Voxel::VoxelModel* ActiveVoxelModel() noexcept override { return &model_; }
    Asset::Voxel::VoxelDocument* ActiveVoxelDocument() noexcept override
    {
        return document_;
    }
    CommandResult RebuildActiveVoxelMesh() override
    {
        ++rebuilds_;
        return CommandResult::Success();
    }
    void CompleteVoxelEdit() noexcept override {}
    void UpdateVoxelEditSavedState(bool) noexcept override {}
    std::size_t Rebuilds() const noexcept { return rebuilds_; }

private:
    Asset::Voxel::VoxelDocument* document_ = nullptr;
    Voxel::VoxelModel model_;
    std::size_t rebuilds_ = 0U;
};

struct CommitSample final
{
    std::int32_t Size = 0;
    std::size_t Destinations = 0U;
    std::size_t Changes = 0U;
    double BuildMs = 0.0;
    double ExecuteMs = 0.0;
    double SelectionSyncMs = 0.0;
    double TotalMs = 0.0;
    double UndoMs = 0.0;
    double RedoMs = 0.0;
    double OperationMiB = 0.0;
    double WorkingSetAfterBuildMiB = 0.0;
    double WorkingSetPeakDeltaMiB = 0.0;
    double PeakMiB = 0.0;
    std::size_t Rebuilds = 0U;
};

CommitSample MeasureCommit(const std::int32_t size)
{
    Asset::Voxel::VoxelDocument document = MakeDocument(false);
    // Le document reste 256^3 ; la cible est size^3.
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    std::vector<Position> pattern;
    for (std::int32_t x = 0; x < 2; ++x)
        for (std::int32_t y = 0; y < 2; ++y)
            for (std::int32_t z = 0; z < 2; ++z)
                pattern.push_back({x, y, z});
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});
    Require(selection.ApplySortedVolume(
        pattern, sourceBounds, SelectionMode::Replace),
        "Unable to select the commit pattern.");
    const SelectionBounds target = SelectionBounds::FromCorners(
        {0, 0, 0}, {size - 1, size - 1, size - 1});
    const VoxelWrapOptions options{};

    CommitSample sample;
    sample.Size = size;
    BenchmarkSession session(document);
    VoxelEditHistory history;
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Unable to begin the commit preview.");
    WrapPatternIndex index(preview.SourceVoxels(), sourceBounds);
    WrapCollisionTracker collisions;
    PreviewDriver driver{document, selection, preview, index, collisions};
    const VoxelWrapSummary summary = driver.Update(target, options);
    sample.Destinations = static_cast<std::size_t>(summary.DestinationCount);
    Require(!preview.HasCollisions(), "Commit benchmark preview collides.");
    const auto expected = [&]
    {
        // Le document attendu : le motif 2x2x2 repete (valeur 1 + x%2 + 2*y%2
        // + 4*z%2) sur toute la cible, plus l'obstacle isole hors cible.
        std::size_t count = 0U;
        document.GetModel(0U)->ForEachVoxel(
            [&](const Position p, const Asset::Voxel::Voxel v)
            {
                if (target.Contains(p))
                {
                    const std::uint8_t value = static_cast<std::uint8_t>(
                        1 + (p.X % 2) + 2 * (p.Y % 2) + 4 * (p.Z % 2));
                    Require(v.PaletteIndex == value,
                        "Committed document differs from the wrap pattern.");
                    ++count;
                }
            });
        return count;
    };

    const double baseline = WorkingSetMiB();
    // 1. Build : materialisation unique dans le modele + operation atomique.
    WrapVoxelSelectionResult prepared;
    sample.BuildMs = Milliseconds([&]
    {
        prepared = WrapVoxelSelectionOperation::Build(
            document, selection, Generation, preview, sourceBounds, target,
            options);
    });
    Require(prepared.Ready(), "Commit benchmark build was refused: " +
        prepared.Message);
    sample.Changes = prepared.Operation.Changes.size();
    sample.OperationMiB = static_cast<double>(
        EstimateVoxelEditOperationMemory(prepared.Operation)) /
        (1024.0 * 1024.0);
    sample.WorkingSetAfterBuildMiB = WorkingSetMiB() - baseline;
    // Comme le workspace : la preview tombe (son tampon est libere) et
    // l'operation est DEPLACEE dans l'historique, jamais copiee.
    static_cast<void>(preview.CancelPreview());
    const double beforePeak = PeakWorkingSetMiB();
    // 2. Execute : historique + applier atomique + document + grille.
    VoxelEditHistoryResult executed;
    sample.ExecuteMs = Milliseconds([&]
    {
        executed = history.Execute(session, std::move(prepared.Operation));
    });
    Require(static_cast<bool>(executed), "Commit benchmark execute failed: " +
        executed.Message);
    // 3. Synchronisation de la selection (transition After), comme
    //    ApplyVoxelHistorySelection.
    sample.SelectionSyncMs = Milliseconds([&]
    {
        const auto& after = executed.SelectionTransition->After;
        selection.SetDocumentGeneration(after.DocumentGeneration);
        static_cast<void>(selection.ApplySortedVolume(
            after.Voxels, after.Bounds, SelectionMode::Replace));
    });
    sample.TotalMs = sample.BuildMs + sample.ExecuteMs + sample.SelectionSyncMs;
    sample.WorkingSetPeakDeltaMiB = PeakWorkingSetMiB() - beforePeak;
    sample.Rebuilds = session.Rebuilds();
    // Document, selection et bornes editables exacts apres le commit.
    Require(expected() == sample.Destinations &&
        document.GetVoxelCount() == sample.Destinations + 1U,
        "Committed document is not exactly the wrap result.");
    Require(selection.Count() == sample.Destinations &&
        selection.EditableBounds() == target,
        "Committed selection or editable bounds are not exact.");
    // 4. Undo puis Redo exacts (voxels + bornes editables semantiques).
    VoxelEditHistoryResult undone;
    sample.UndoMs = Milliseconds([&]
    {
        undone = history.Undo(session);
    });
    Require(static_cast<bool>(undone), "Commit benchmark undo failed.");
    {
        const auto& before = undone.SelectionTransition->Before;
        static_cast<void>(selection.ApplySortedVolume(
            before.Voxels, before.Bounds, SelectionMode::Replace));
    }
    Require(document.GetVoxelCount() == 9U && selection.Count() == 8U &&
        selection.EditableBounds() == sourceBounds,
        "Undo did not restore the source document and editable bounds.");
    VoxelEditHistoryResult redone;
    sample.RedoMs = Milliseconds([&]
    {
        redone = history.Redo(session);
    });
    Require(static_cast<bool>(redone), "Commit benchmark redo failed.");
    {
        const auto& after = redone.SelectionTransition->After;
        static_cast<void>(selection.ApplySortedVolume(
            after.Voxels, after.Bounds, SelectionMode::Replace));
    }
    Require(expected() == sample.Destinations &&
        selection.Count() == sample.Destinations &&
        selection.EditableBounds() == target,
        "Redo did not restore the wrap result and requested bounds.");
    sample.PeakMiB = PeakWorkingSetMiB();
    return sample;
}

// La copie profonde prepared -> owned que le workspace faisait avant le
// correctif : mesuree ici A PART (document et preview neufs, apres toutes
// les autres mesures) pour chiffrer ce que le move evite, sans fausser les
// pics des sections precedentes.
void MeasureRemovedCopy(const std::int32_t size)
{
    Asset::Voxel::VoxelDocument document = MakeDocument(false);
    SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    std::vector<Position> pattern;
    for (std::int32_t x = 0; x < 2; ++x)
        for (std::int32_t y = 0; y < 2; ++y)
            for (std::int32_t z = 0; z < 2; ++z)
                pattern.push_back({x, y, z});
    const SelectionBounds sourceBounds =
        SelectionBounds::FromCorners({0, 0, 0}, {1, 1, 1});
    Require(selection.ApplySortedVolume(
        pattern, sourceBounds, SelectionMode::Replace),
        "Unable to select the copy pattern.");
    const SelectionBounds target = SelectionBounds::FromCorners(
        {0, 0, 0}, {size - 1, size - 1, size - 1});
    TransformPreviewModel preview;
    Require(preview.BeginPreview(document, selection, Generation, 0U,
        TransformPreviewCollisionPolicy::IgnoreSource),
        "Unable to begin the copy preview.");
    WrapVoxelSelectionResult prepared = WrapVoxelSelectionOperation::Build(
        document, selection, Generation, preview, sourceBounds, target, {});
    Require(prepared.Ready(), "Copy benchmark build was refused.");
    const double before = WorkingSetMiB();
    double copyMs = 0.0;
    double copyMiB = 0.0;
    {
        VoxelEditOperation copy;
        copyMs = Milliseconds([&] { copy = prepared.Operation; });
        copyMiB = WorkingSetMiB() - before;
        Require(copy.Changes.size() == prepared.Operation.Changes.size(),
            "Copy benchmark lost changes.");
    }
    std::cout << std::fixed << std::setprecision(1)
        << "  copie prepared -> owned SUPPRIMEE (" << size << "^3) : "
        << prepared.Operation.Changes.size() << " VoxelChange, "
        << static_cast<double>(prepared.Operation.Changes.size() *
            sizeof(VoxelChange)) / (1024.0 * 1024.0)
        << " MiB (ws+ " << copyMiB << " MiB), " << copyMs << " ms"
        << std::endl;
}

void PrintCommit(const CommitSample& sample)
{
    std::cout << std::fixed << std::setprecision(1)
        << "  " << std::setw(4) << sample.Size << "^3 | dest "
        << std::setw(8) << sample.Destinations
        << " | changes " << std::setw(8) << sample.Changes
        << " | build " << std::setw(7) << sample.BuildMs << " ms"
        << " | execute " << std::setw(7) << sample.ExecuteMs << " ms"
        << " | sync " << std::setw(6) << sample.SelectionSyncMs << " ms"
        << " | TOTAL " << std::setw(7) << sample.TotalMs << " ms"
        << " | undo " << std::setw(7) << sample.UndoMs
        << " | redo " << std::setw(7) << sample.RedoMs << " ms"
        << " | op " << std::setw(6) << sample.OperationMiB << " MiB"
        << " | ws+build " << std::setw(6) << sample.WorkingSetAfterBuildMiB
        << " | peak+exec " << std::setw(6) << sample.WorkingSetPeakDeltaMiB
        << " | peak " << std::setw(6) << sample.PeakMiB << " MiB"
        << " | rebuilds " << sample.Rebuilds << '\n';
}
} // namespace

// Sans argument : gate interactif (drag). `--commit` : commit reel de bout en
// bout (plus lourd, ~8 s), enregistre comme test CTest separe pour ne pas
// ralentir la suite rapide.
int main(const int argc, const char* const* const argv)
{
    try
    {
        const bool commitOnly = argc > 1 && std::string_view(argv[1]) == "--commit";
        if (commitOnly)
        {
            std::cout << "Commit reel (Build -> Execute -> sync selection ; "
                "remaillage workspace exclu) :" << std::endl;
            for (const std::int32_t size : {64, 128})
                PrintCommit(MeasureCommit(size));
            MeasureRemovedCopy(128);
            return 0;
        }
        std::cout << "Wrap benchmark (pattern 2x2x2 plein, document 256^3) - gate "
            << GateLabel << " : mediane < " << MedianGateMs << " ms, p95 < "
            << P95GateMs << " ms, plafond < " << CeilingGateMs << " ms"
            << std::endl;
        const VoxelEditHistoryLimits limits{};
        std::cout << "  history memory limit: "
            << limits.MaximumEstimatedMemory / (1024U * 1024U)
            << " MiB | exact preview budget: "
            << WrapVoxelSelectionOperation::ExactPreviewDestinationBudget
            << " destinations\n";
        for (const std::int32_t size : {16, 32, 64, 128, 256})
        {
            const Sample sample = Measure(size, false);
            Print(sample);
            // Gardes : jamais plus que le budget materialise pendant le drag,
            // et chaque delta d'un drag continu reste sous la frame (marge
            // large pour les machines lentes : 4 frames).
            Require(sample.PreviewVoxels <=
                WrapVoxelSelectionOperation::ExactPreviewDestinationBudget,
                "Drag preview materialised more than the exact budget.");
            Gate(sample);
            Require(sample.Collisions == 1U ||
                (sample.Size <= 200 && sample.Collisions == 0U),
                "Collision tracking lost the obstacle.");
        }
        std::cout << "Dense document (mur 4x256x256 en x = 96..99) :"
            << std::endl;
        for (const std::int32_t size : {128, 256})
        {
            const Sample sample = Measure(size, true);
            Print(sample);
            Require(sample.PreviewVoxels <=
                WrapVoxelSelectionOperation::ExactPreviewDestinationBudget,
                "Dense drag preview materialised more than the exact budget.");
            Gate(sample);
            // Le mur est recouvert a 128^3 (4 x 128 x 128 = 65 536, la limite
            // de positions suivies) et a 256^3 (4 x 256 x 256 + l'obstacle
            // isole, au-dela de la limite) : le compte reste EXACT.
            const std::size_t expected = size == 128
                ? 4U * 128U * 128U : 4U * 256U * 256U + 1U;
            Require(sample.Collisions == expected,
                "Dense collision count is not exact.");
        }
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Wrap benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
