// PERF-FOUNDATION lot 0 — baseline mesurable des quatre chemins que la mission
// du 05/08 doit corriger. Fixture de profilage, deliberement absente de CTest.
//
// Ce banc complete les deux bancs existants plutot que de les remplacer :
//   - IncrementalEditBenchmark  : cubes denses, mediane, mesh uniquement ;
//   - StampPlacementPipeline    : allocations detaillees, moyenne et maximum.
// Il apporte ce qui manque a la mission : percentiles p50/p95/p99, comptage des
// echantillons au-dela du budget de 16,67 ms, scenes CREUSES (le risque
// probeRegion ne se voit que la), et le compositeur de preview exacte, qui
// n'etait mesure nulle part.
//
// Tous les chemins mesures ici sont sans interface : Compose et Build sont des
// fonctions statiques, aucun GPU ni ImGui n'est requis.

#include "SmartTools/SmartToolExactPreviewComposer.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/StampResourceLimits.h"
#include "VoxelStamps/VoxelStamp.h"

#include "StampPlacementBenchmarkMetrics.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"
#include "VoxelForge/Mesh/VoxelDocumentMeshCache.h"
#include "VoxelForge/Mesh/VoxelMeshBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace
{
using VoxelForge::Benchmarks::AllocationScope;
using VoxelForge::Benchmarks::AllocationSnapshot;

using VoxelForge::Asset::Vox::DefaultVoxPalette;
using VoxelForge::Asset::Vox::VoxModel;
using VoxelForge::Asset::Vox::VoxModelMetadata;
using VoxelForge::Asset::Vox::VoxVoxel;
using VoxelForge::Asset::Voxel::VoxDocumentLoader;
using VoxelForge::Asset::Voxel::VoxelDocument;
using VoxelForge::Asset::Voxel::VoxelDocumentChange;
using VoxelForge::Asset::Voxel::VoxelPosition;
using VoxelForge::Mesh::VoxelDocumentMeshCache;
using VoxelForge::Mesh::VoxelMeshBuilder;

namespace Stamps = VoxelForge::Editor::Stamps;

// Budget d'une frame a 60 FPS. Un chemin qui le depasse a lui seul ne peut pas
// tenir dans une interaction continue.
constexpr double FrameBudgetMilliseconds = 16.67;

// ---------------------------------------------------------------------------
// Agregation
// ---------------------------------------------------------------------------

struct Measurement final
{
    std::vector<double> Milliseconds;
    AllocationSnapshot Allocations{};

    [[nodiscard]] double Percentile(const double ratio) const
    {
        if (Milliseconds.empty()) return 0.0;
        std::vector<double> sorted = Milliseconds;
        std::sort(sorted.begin(), sorted.end());
        const double position =
            ratio * static_cast<double>(sorted.size());
        std::size_t index = static_cast<std::size_t>(std::ceil(position));
        if (index == 0U) index = 1U;
        if (index > sorted.size()) index = sorted.size();
        return sorted[index - 1U];
    }

    [[nodiscard]] double Maximum() const
    {
        return Milliseconds.empty()
            ? 0.0
            : *std::max_element(Milliseconds.begin(), Milliseconds.end());
    }

    [[nodiscard]] std::size_t OverBudgetCount() const
    {
        return static_cast<std::size_t>(std::count_if(
            Milliseconds.begin(), Milliseconds.end(),
            [](const double value)
            { return value > FrameBudgetMilliseconds; }));
    }
};

// Mesure `runs` executions. Les allocations sont comptees sur la premiere
// execution uniquement : les suivantes reutilisent des tampons deja chauds et
// donneraient une image trop favorable.
template <typename Callable>
Measurement Measure(const std::size_t runs, const Callable& callable)
{
    Measurement measurement;
    measurement.Milliseconds.reserve(runs);
    for (std::size_t run = 0U; run < runs; ++run)
    {
        if (run == 0U)
        {
            const AllocationScope scope;
            const auto start = std::chrono::steady_clock::now();
            callable();
            const auto stop = std::chrono::steady_clock::now();
            measurement.Allocations = scope.Snapshot();
            measurement.Milliseconds.push_back(
                std::chrono::duration<double, std::milli>(stop - start)
                    .count());
            continue;
        }
        const auto start = std::chrono::steady_clock::now();
        callable();
        const auto stop = std::chrono::steady_clock::now();
        measurement.Milliseconds.push_back(
            std::chrono::duration<double, std::milli>(stop - start).count());
    }
    return measurement;
}

void Report(
    const char* const scene,
    const char* const shape,
    const std::uint64_t voxels,
    const std::uint32_t edge,
    const char* const phase,
    const Measurement& measurement)
{
    std::printf("%s,%s,%llu,%u,%s,%zu,%.3f,%.3f,%.3f,%.3f,%zu,%llu,%llu,%llu\n",
        scene, shape, static_cast<unsigned long long>(voxels), edge, phase,
        measurement.Milliseconds.size(),
        measurement.Percentile(0.50), measurement.Percentile(0.95),
        measurement.Percentile(0.99), measurement.Maximum(),
        measurement.OverBudgetCount(),
        static_cast<unsigned long long>(
            measurement.Allocations.AllocationCount),
        static_cast<unsigned long long>(
            measurement.Allocations.AllocatedBytes),
        static_cast<unsigned long long>(
            measurement.Allocations.PeakLiveBytes));
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Scenes
// ---------------------------------------------------------------------------

VoxelDocument BuildDocument(
    const std::uint32_t edge, std::vector<VoxVoxel> voxels)
{
    VoxModel source;
    source.Version = 150U;
    source.Palette = DefaultVoxPalette();
    source.Models.push_back(VoxModelMetadata{
        {edge, edge, edge}, std::move(voxels)});
    source.DeclaredModelCount = 1U;
    auto loaded = VoxDocumentLoader{}.Build(source, "baseline.vox");
    if (!loaded.Succeeded() || !loaded.Document)
    {
        std::fprintf(stderr, "Fixture failed: %s\n", loaded.Message.c_str());
        std::exit(1);
    }
    return std::move(*loaded.Document);
}

// Cube plein : le cas dense de reference, huit indices de palette.
VoxelDocument DenseCube(const std::uint32_t edge)
{
    std::vector<VoxVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(edge) * edge * edge);
    for (std::uint32_t z = 0U; z < edge; ++z)
        for (std::uint32_t y = 0U; y < edge; ++y)
            for (std::uint32_t x = 0U; x < edge; ++x)
                voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z),
                    static_cast<std::uint8_t>(1U + (x + y + z) % 8U)});
    return BuildDocument(edge, std::move(voxels));
}

// Boite large et vide : `count` voxels repartis sur toute l'etendue. C'est la
// scene qui revele le defaut probeRegion — le volume est enorme, la matiere
// negligeable.
VoxelDocument SparseBox(const std::uint32_t edge, const std::uint32_t count)
{
    std::vector<VoxVoxel> voxels;
    voxels.reserve(count);
    // Pas premier vis-a-vis de l'arete : la repartition traverse les chunks.
    const std::uint32_t stride =
        std::max<std::uint32_t>(1U, (edge * edge * edge) / count);
    std::uint32_t linear = 0U;
    for (std::uint32_t index = 0U; index < count; ++index)
    {
        const std::uint32_t x = linear % edge;
        const std::uint32_t y = (linear / edge) % edge;
        const std::uint32_t z = (linear / (edge * edge)) % edge;
        voxels.push_back({static_cast<std::uint8_t>(x),
            static_cast<std::uint8_t>(y),
            static_cast<std::uint8_t>(z),
            static_cast<std::uint8_t>(1U + index % 8U)});
        linear += stride;
    }
    return BuildDocument(edge, std::move(voxels));
}

// Deux amas denses aux coins opposes : chunks tres eloignes, tout le reste vide.
VoxelDocument FarClusters(const std::uint32_t edge, const std::uint32_t block)
{
    std::vector<VoxVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(block) * block * block * 2U);
    for (std::uint32_t z = 0U; z < block; ++z)
        for (std::uint32_t y = 0U; y < block; ++y)
            for (std::uint32_t x = 0U; x < block; ++x)
            {
                voxels.push_back({static_cast<std::uint8_t>(x),
                    static_cast<std::uint8_t>(y),
                    static_cast<std::uint8_t>(z), 1U});
                voxels.push_back({
                    static_cast<std::uint8_t>(edge - 1U - x),
                    static_cast<std::uint8_t>(edge - 1U - y),
                    static_cast<std::uint8_t>(edge - 1U - z), 2U});
            }
    return BuildDocument(edge, std::move(voxels));
}

// ---------------------------------------------------------------------------
// Fixtures des chemins mesures
// ---------------------------------------------------------------------------

// Empreinte type d'un pinceau : un bloc de cellules changees, comme ce que le
// planner produit pour un Brush cube.
std::vector<VoxelDocumentChange> BrushChanges(
    const VoxelDocument& document,
    const std::int32_t centre,
    const std::int32_t radius)
{
    std::vector<VoxelDocumentChange> changes;
    const std::int32_t span = radius * 2 + 1;
    changes.reserve(
        static_cast<std::size_t>(span) * static_cast<std::size_t>(span) *
        static_cast<std::size_t>(span));
    for (std::int32_t z = centre - radius; z <= centre + radius; ++z)
        for (std::int32_t y = centre - radius; y <= centre + radius; ++y)
            for (std::int32_t x = centre - radius; x <= centre + radius; ++x)
            {
                const VoxelPosition position{x, y, z};
                const auto existing = document.GetVoxel(position);
                changes.push_back({
                    .SubModelIndex = 0U,
                    .Position = position,
                    .ExistedBefore = existing.has_value(),
                    .PaletteIndexBefore =
                        existing ? existing->PaletteIndex : std::uint8_t{0U},
                    .ExistsAfter = true,
                    .PaletteIndexAfter = 12U});
            }
    return changes;
}

Stamps::VoxelStamp BuildStamp(const std::int32_t edge)
{
    std::vector<Stamps::StampVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(edge) * edge * edge);
    for (std::int32_t z = 0; z < edge; ++z)
        for (std::int32_t y = 0; y < edge; ++y)
            for (std::int32_t x = 0; x < edge; ++x)
                voxels.push_back({{x, y, z},
                    static_cast<std::uint8_t>((x + y + z) % 4)});
    std::vector<Stamps::StampPaletteEntry> palette;
    for (std::uint8_t index = 0U; index < 4U; ++index)
        palette.push_back({index,
            {static_cast<std::uint8_t>(40U + index * 30U), 120U, 200U, 255U}});

    Stamps::StampValidationResult validation{};
    const auto stamp = Stamps::VoxelStamp::TryCreate(
        {VoxelForge::Core::UUID{0x50455246U}, "perf-foundation-stamp"},
        {{0, 0, 0}, {edge - 1, edge - 1, edge - 1},
         {static_cast<std::uint32_t>(edge), static_cast<std::uint32_t>(edge),
          static_cast<std::uint32_t>(edge)}},
        {.RequestedMode = Stamps::StampPivotMode::Corner,
         .ResolvedMode = Stamps::StampPivotMode::Corner,
         .LocalPosition = {}},
        {}, palette, std::move(voxels),
        Stamps::DefaultStampResourceLimits(), &validation);
    if (!stamp || !validation.IsValid())
    {
        std::fprintf(stderr, "Stamp fixture failed.\n");
        std::exit(2);
    }
    return *stamp;
}

// ---------------------------------------------------------------------------

struct Scene final
{
    const char* Name;
    const char* Shape;
    std::uint32_t Edge;
    VoxelDocument (*Make)(std::uint32_t);
};

std::size_t RunsFor(const std::uint64_t voxels)
{
    if (voxels <= 50'000U) return 21U;
    if (voxels <= 200'000U) return 11U;
    return 7U;
}

void MeasureScene(
    const char* const name,
    const char* const shape,
    VoxelDocument document,
    const Stamps::VoxelStamp& stamp)
{
    const std::uint64_t voxels = document.GetVoxelCount();
    const auto dimensions = document.GetDimensions(0U);
    const std::uint32_t edge = dimensions ? dimensions->X : 0U;
    const std::size_t runs = RunsFor(voxels);
    const std::int32_t centre = static_cast<std::int32_t>(edge / 2U);

    // 1. Rebuild complet : le cout que le cache incremental evite.
    Report(name, shape, voxels, edge, "mesh_full",
        Measure(runs,
            [&document]
            {
                const auto built = VoxelMeshBuilder::Build(document);
                if (!built.Succeeded) std::exit(3);
            }));

    // 2. Cout reel d'une edition : SetVoxel puis Synchronize incremental.
    {
        VoxelDocumentMeshCache cache;
        if (!cache.Synchronize(document, 1U).Succeeded) std::exit(4);
        bool toggle = false;
        Report(name, shape, voxels, edge, "mesh_sync_edit",
            Measure(runs,
                [&document, &cache, &toggle, centre]
                {
                    toggle = !toggle;
                    if (!document.SetVoxel({centre, centre, centre},
                            toggle ? 9U : 10U).Succeeded)
                        std::exit(5);
                    if (!cache.Synchronize(document, 1U).Succeeded)
                        std::exit(6);
                }));
    }

    // 3. Compositeur de preview exacte. Il copie tout le document puis remaille
    //    tout : le cout doit donc suivre la TAILLE DU DOCUMENT et non celle du
    //    pinceau. Les deux tailles d'empreinte le prouvent ou l'infirment.
    for (const std::int32_t radius : {1, 4})
    {
        const auto changes = BrushChanges(document, centre, radius);
        const std::string phase =
            "preview_compose_r" + std::to_string(radius);
        Report(name, shape, voxels, edge, phase.c_str(),
            Measure(runs,
                [&document, &changes]
                {
                    const auto composed =
                        VoxelForge::Editor::SmartToolExactPreviewComposer::
                            Compose(document,
                                std::span<const VoxelDocumentChange>{changes});
                    if (!composed.Succeeded()) std::exit(7);
                }));
    }

    // 4. Planification d'un placement de Stamp. Build parcourt tout le document
    //    pour relever les indices de palette occupes : le cout doit lui aussi
    //    suivre la taille du document, pour un Stamp constant.
    Report(name, shape, voxels, edge, "stamp_plan",
        Measure(runs,
            [&document, &stamp, centre]
            {
                const auto plan = Stamps::StampPlacementPlanner::Build({
                    .Stamp = &stamp,
                    .Document = &document,
                    .DocumentGeneration = 1U,
                    .TargetSubModel = 0U,
                    .Transform = {.TargetPivot = {
                        centre * Stamps::StampFixedPoint::UnitsPerVoxel,
                        centre * Stamps::StampFixedPoint::UnitsPerVoxel,
                        centre * Stamps::StampFixedPoint::UnitsPerVoxel}},
                    .CollisionPolicy = Stamps::StampCollisionPolicy::Overwrite});
                if (plan.Voxels.empty()) std::exit(8);
            }));

    // 5. Meme mesure, avec le cache du lot 2. Le premier appel paie encore le
    //    parcours ; les suivants ne doivent plus rien payer du tout.
    {
        Stamps::StampDocumentPaletteCache paletteCache;
        Report(name, shape, voxels, edge, "stamp_plan_cached",
            Measure(runs,
                [&document, &stamp, &paletteCache, centre]
                {
                    const auto plan = Stamps::StampPlacementPlanner::Build({
                        .Stamp = &stamp,
                        .Document = &document,
                        .DocumentGeneration = 1U,
                        .TargetSubModel = 0U,
                        .Transform = {.TargetPivot = {
                            centre * Stamps::StampFixedPoint::UnitsPerVoxel,
                            centre * Stamps::StampFixedPoint::UnitsPerVoxel,
                            centre * Stamps::StampFixedPoint::UnitsPerVoxel}},
                        .CollisionPolicy =
                            Stamps::StampCollisionPolicy::Overwrite,
                        .OccupiedPaletteCache = &paletteCache});
                    if (plan.Voxels.empty()) std::exit(9);
                }));
    }
}

} // namespace

int main()
{
    std::printf(
        "scene,shape,voxels,edge,phase,runs,p50_ms,p95_ms,p99_ms,max_ms,"
        "over_budget,alloc_count,alloc_bytes,peak_bytes\n");

    const Stamps::VoxelStamp stamp = BuildStamp(8);

    // Denses : la progression de reference, 15k -> 1M.
    for (const std::uint32_t edge : {25U, 37U, 47U, 80U, 100U})
        MeasureScene("dense", "cube", DenseCube(edge), stamp);

    // Creuses : meme quantite de matiere, boites de plus en plus grandes.
    // Invariant vise : 1 000 voxels doivent couter pareil en 64 cube et en
    // 256 cube. Aujourd'hui, non.
    for (const std::uint32_t edge : {64U, 128U, 256U})
        MeasureScene("sparse1k", "box", SparseBox(edge, 1'000U), stamp);
    for (const std::uint32_t edge : {64U, 128U, 256U})
        MeasureScene("sparse10k", "box", SparseBox(edge, 10'000U), stamp);

    // Chunks eloignes : deux amas, tout le reste vide.
    MeasureScene("clusters", "corners", FarClusters(128U, 16U), stamp);
    MeasureScene("clusters", "corners", FarClusters(256U, 16U), stamp);

    return 0;
}
