// PERF-FOUNDATION lot 2 : le cache des index de palette occupés doit rendre
// exactement ce que rendait le parcours complet, et ne se recalculer que sur
// une vraie modification du document. Les compteurs hits/misses rendent la
// réutilisation observable sans chronomètre : aucun test ne dépend du temps.

#include "VoxelStamps/Palette/StampDocumentPaletteCache.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/StampResourceLimits.h"
#include "VoxelStamps/VoxelStamp.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument(
    std::vector<Asset::Vox::VoxVoxel> voxels)
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {16U, 16U, 16U},
        .Voxels = std::move(voxels)});
    auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "palette-cache.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Palette cache document fixture must build.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeStamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x50414C43U}, "palette-cache-stamp"},
        {{0, 0, 0}, {1, 0, 0}, {2U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Corner,
         .ResolvedMode = StampPivotMode::Corner,
         .LocalPosition = {}},
        {}, {{0U, {200U, 30U, 30U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Palette cache Stamp fixture must be valid.");
    return *stamp;
}

std::size_t OccupiedCount(const StampOccupiedPaletteIndices& indices)
{
    std::size_t count = 0U;
    for (const bool occupied : indices)
        if (occupied) ++count;
    return count;
}

// Le cache ne doit jamais diverger du parcours de référence, quel que soit le
// contenu du document.
void TestMatchesReferenceScan()
{
    auto empty = MakeDocument({});
    StampDocumentPaletteCache cache;
    Require(OccupiedCount(cache.Resolve(empty)) == 0U,
        "An empty document occupies no palette index.");

    auto single = MakeDocument({{.X = 1U, .Y = 1U, .Z = 1U, .ColorIndex = 5U}});
    StampDocumentPaletteCache singleCache;
    const auto& singleIndices = singleCache.Resolve(single);
    Require(singleIndices == StampDocumentPaletteCache::Scan(single) &&
            OccupiedCount(singleIndices) == 1U,
        "One colour must be reported exactly once.");

    auto several = MakeDocument({
        {.X = 0U, .Y = 0U, .Z = 0U, .ColorIndex = 3U},
        {.X = 1U, .Y = 0U, .Z = 0U, .ColorIndex = 7U},
        {.X = 2U, .Y = 0U, .Z = 0U, .ColorIndex = 3U}});
    StampDocumentPaletteCache severalCache;
    const auto& severalIndices = severalCache.Resolve(several);
    Require(severalIndices == StampDocumentPaletteCache::Scan(several) &&
            OccupiedCount(severalIndices) == 2U,
        "Repeated colours must not be counted twice.");
}

// La révision est la seule chose qui invalide : ajout, peinture, suppression
// de la dernière occurrence d'une couleur, Undo et Redo passent tous par elle.
void TestRevisionInvalidatesAndNothingElse()
{
    auto document = MakeDocument({
        {.X = 0U, .Y = 0U, .Z = 0U, .ColorIndex = 4U}});
    StampDocumentPaletteCache cache;

    static_cast<void>(cache.Resolve(document));
    Require(cache.MissCount() == 1U && cache.HitCount() == 0U,
        "The first survey must be computed.");

    for (int repeat = 0; repeat < 25; ++repeat)
        static_cast<void>(cache.Resolve(document));
    Require(cache.MissCount() == 1U && cache.HitCount() == 25U,
        "An unchanged document must never be surveyed twice.");

    Require(document.SetVoxel({1, 0, 0}, 9U).Succeeded,
        "Adding a voxel must succeed.");
    const auto& afterAdd = cache.Resolve(document);
    Require(cache.MissCount() == 2U && afterAdd[9U],
        "A new revision must trigger exactly one recomputation.");

    Require(document.ReplaceVoxelColor({1, 0, 0}, 11U).Succeeded,
        "Painting a voxel must succeed.");
    const auto& afterPaint = cache.Resolve(document);
    Require(cache.MissCount() == 3U && afterPaint[11U] && !afterPaint[9U],
        "Painting away the last voxel of a colour must free that index.");

    Require(document.RemoveVoxel({1, 0, 0}).Succeeded,
        "Removing a voxel must succeed.");
    const auto& afterRemove = cache.Resolve(document);
    Require(cache.MissCount() == 4U && !afterRemove[11U] && afterRemove[4U],
        "Removing the last voxel of a colour must free that index.");

    // Un autre document, même cache : l'identité doit primer.
    auto other = MakeDocument({{.X = 0U, .Y = 0U, .Z = 0U, .ColorIndex = 2U}});
    const auto& otherIndices = cache.Resolve(other);
    Require(otherIndices[2U] && !otherIndices[4U],
        "Switching document must not serve the previous survey.");
}

// Contrat central du lot : avec ou sans cache, le plan produit doit être
// rigoureusement identique.
void TestPlannerResultIsIdenticalWithAndWithoutCache()
{
    auto document = MakeDocument({
        {.X = 5U, .Y = 0U, .Z = 5U, .ColorIndex = 6U},
        {.X = 6U, .Y = 0U, .Z = 5U, .ColorIndex = 8U}});
    const VoxelStamp stamp = MakeStamp();
    const StampPlacementPlannerRequest base{
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 1U,
        .TargetSubModel = 0U,
        .Transform = {.TargetPivot = {
            3 * StampFixedPoint::UnitsPerVoxel,
            0,
            3 * StampFixedPoint::UnitsPerVoxel}},
        .CollisionPolicy = StampCollisionPolicy::Overwrite};

    const StampPlacementPlan reference = StampPlacementPlanner::Build(base);

    StampDocumentPaletteCache cache;
    StampPlacementPlannerRequest cached = base;
    cached.OccupiedPaletteCache = &cache;
    const StampPlacementPlan first = StampPlacementPlanner::Build(cached);
    const StampPlacementPlan second = StampPlacementPlanner::Build(cached);

    Require(first.Voxels == reference.Voxels &&
            first.PaletteMapping == reference.PaletteMapping &&
            first.PaletteStatus == reference.PaletteStatus &&
            first.Statistics == reference.Statistics &&
            first.CanCommit == reference.CanCommit,
        "A cached survey must not change the plan in any way.");
    Require(second.Voxels == first.Voxels &&
            second.PaletteMapping == first.PaletteMapping,
        "A second cached build must be identical to the first.");
    Require(cache.MissCount() == 1U && cache.HitCount() == 1U,
        "Two builds on an unchanged document must survey it once.");

    // Rotation, miroir et déplacement de la cible ne touchent pas au document :
    // ils ne doivent jamais provoquer de nouveau parcours.
    StampPlacementPlannerRequest moved = cached;
    moved.Transform.QuarterTurns = 1U;
    moved.Transform.Mirror = StampPlacementMirrorMode::X;
    moved.Transform.TargetPivot = {
        4 * StampFixedPoint::UnitsPerVoxel, 0,
        4 * StampFixedPoint::UnitsPerVoxel};
    static_cast<void>(StampPlacementPlanner::Build(moved));
    Require(cache.MissCount() == 1U && cache.HitCount() == 2U,
        "Rotation, mirror and target changes must not resurvey the document.");
}

} // namespace

int main()
{
    try
    {
        TestMatchesReferenceScan();
        TestRevisionInvalidatesAndNothingElse();
        TestPlannerResultIsIdenticalWithAndWithoutCache();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp document palette cache tests passed.\n";
    return EXIT_SUCCESS;
}
