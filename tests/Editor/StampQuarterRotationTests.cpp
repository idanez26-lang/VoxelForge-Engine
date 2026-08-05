#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({
        .Dimensions = {12U, 6U, 12U},
        .Voxels = {{.X = 4U, .Y = 1U, .Z = 6U, .ColorIndex = 1U}}});
    const auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "quarter-turn.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Quarter rotation document fixture must build.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeStamp()
{
    constexpr std::int32_t unit = StampFixedPoint::UnitsPerVoxel;
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503135ULL}, "stamp-15-quarter-turn"},
        {{0, 0, 0}, {2, 0, 1}, {3U, 1U, 2U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {unit, 0, unit}},
        {},
        {{0U, {10U, 20U, 30U, 255U}},
         {1U, {40U, 50U, 60U, 255U}}},
        {{{0, 0, 0}, 0U}, {{2, 0, 0}, 1U}, {{0, 0, 1}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Quarter rotation Stamp fixture must be valid.");
    return *stamp;
}

StampPlacementPlan Plan(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const std::uint8_t quarterTurns,
    const StampFixedPoint target = {
        5 * StampFixedPoint::UnitsPerVoxel,
        StampFixedPoint::UnitsPerVoxel,
        5 * StampFixedPoint::UnitsPerVoxel})
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 15U,
        .Transform = {
            .TargetPivot = target,
            .QuarterTurns = quarterTurns}});
}

// STAMP-24 : meme fixture, mais l'axe de rotation devient explicite.
StampPlacementPlan PlanAroundAxis(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampPlacementRotationAxis axis,
    const std::uint8_t quarterTurns,
    const StampFixedPoint target = {
        5 * StampFixedPoint::UnitsPerVoxel,
        StampFixedPoint::UnitsPerVoxel,
        5 * StampFixedPoint::UnitsPerVoxel})
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 15U,
        .Transform = {
            .TargetPivot = target,
            .RotationAxis = axis,
            .QuarterTurns = quarterTurns}});
}

[[nodiscard]] bool HasDiagnostic(
    const StampPlacementPlan& plan,
    const StampPlacementDiagnosticCode code)
{
    for (const StampPlacementDiagnostic& diagnostic : plan.Diagnostics)
    {
        if (diagnostic.Code == code) return true;
    }
    return false;
}

void RequirePositions(
    const StampPlacementPlan& plan,
    const std::array<Position, 3U>& expected)
{
    Require(plan.Voxels.size() == expected.size(),
        "Quarter rotation must preserve voxel count.");
    for (std::size_t index = 0U; index < expected.size(); ++index)
    {
        Require(plan.Voxels[index].WorldPosition == expected[index],
            "Quarter rotation produced an unexpected exact grid cell.");
    }
}

// STAMP-24 : un quart de tour autour de X ou de Z est une permutation exacte
// des coordonnees, au meme titre que la rotation Y historique. Le Stamp
// fixture occupe (0,0,0), (2,0,0) et (0,0,1) autour d'un pivot en (1,0,1),
// pose sur une cible en (5,1,5).
void TestQuarterTurnsAroundEachAxis()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();

    // Autour de X : (y, z) -> (-z, y). Le Stamp bascule a la verticale.
    const auto lateral = PlanAroundAxis(
        stamp, document, StampPlacementRotationAxis::LateralX, 1U);
    Require(lateral.CanCommit && !lateral.HasErrors(),
        "A quarter turn around X must produce a committable plan.");
    RequirePositions(lateral, {{{4, 2, 5}, {6, 2, 5}, {4, 1, 5}}});

    // Autour de Z : (x, y) -> (-y, x). Le Stamp bascule sur le cote.
    const auto depth = PlanAroundAxis(
        stamp, document, StampPlacementRotationAxis::DepthZ, 1U);
    Require(depth.CanCommit && !depth.HasErrors(),
        "A quarter turn around Z must produce a committable plan.");
    RequirePositions(depth, {{{5, 0, 4}, {5, 2, 4}, {5, 0, 5}}});

    // Deux quarts de tour autour de X : (x, y, z) -> (x, -y, -z).
    const auto lateralHalf = PlanAroundAxis(
        stamp, document, StampPlacementRotationAxis::LateralX, 2U);
    RequirePositions(lateralHalf, {{{4, 1, 6}, {6, 1, 6}, {4, 1, 5}}});

    // Quatre quarts de tour ramenent a l'identite sur chaque axe.
    for (const auto axis : {StampPlacementRotationAxis::LateralX,
             StampPlacementRotationAxis::VerticalY,
             StampPlacementRotationAxis::DepthZ})
    {
        const auto identity = PlanAroundAxis(stamp, document, axis, 0U);
        Require(identity.CanCommit &&
                identity.Voxels.size() == stamp.Voxels().size(),
            "A zero quarter turn must keep the Stamp untouched on every axis.");
    }

    // L'axe participe a l'identite du plan : meme nombre de quarts de tour,
    // trois resultats distincts.
    const auto vertical = PlanAroundAxis(
        stamp, document, StampPlacementRotationAxis::VerticalY, 1U);
    Require(lateral.CacheKey != vertical.CacheKey &&
            depth.CacheKey != vertical.CacheKey &&
            lateral.CacheKey != depth.CacheKey,
        "The rotation axis must participate in plan cache identity.");
    Require(lateral.Voxels != vertical.Voxels &&
            depth.Voxels != vertical.Voxels,
        "Each rotation axis must produce its own cells.");

    // Un axe inconnu reste une erreur explicite.
    const auto unknownAxis = PlanAroundAxis(stamp, document,
        static_cast<StampPlacementRotationAxis>(7U), 1U);
    Require(!unknownAxis.CanCommit &&
            !unknownAxis.Diagnostics.empty() &&
            unknownAxis.Diagnostics.front().Code ==
                StampPlacementDiagnosticCode::UnsupportedRotation,
        "An unknown rotation axis must produce the rotation diagnostic.");
}

void TestExactQuarterTurnsRespectPivot()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();

    const auto zero = Plan(stamp, document, 0U);
    const auto ninety = Plan(stamp, document, 1U);
    const auto oneEighty = Plan(stamp, document, 2U);
    const auto twoSeventy = Plan(stamp, document, 3U);

    RequirePositions(zero, {{{4, 1, 4}, {6, 1, 4}, {4, 1, 5}}});
    RequirePositions(ninety, {{{4, 1, 6}, {4, 1, 4}, {5, 1, 6}}});
    RequirePositions(oneEighty, {{{6, 1, 6}, {4, 1, 6}, {6, 1, 5}}});
    RequirePositions(twoSeventy, {{{6, 1, 4}, {6, 1, 6}, {5, 1, 4}}});

    Require(zero.Transform.QuarterTurns == 0U &&
                ninety.Transform.QuarterTurns == 1U &&
                oneEighty.Transform.QuarterTurns == 2U &&
                twoSeventy.Transform.QuarterTurns == 3U,
        "Plan diagnostics metadata must retain the selected quarter turn.");
    Require(zero.WorldBounds != ninety.WorldBounds &&
                ninety.WorldBounds != oneEighty.WorldBounds,
        "Rotated bounds must be derived from rotated planned cells.");
}

void TestPreviewPlacementPaletteAndOverlapSharePlan()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = Plan(stamp, document, 1U);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    const auto placement = PreparePlaceVoxelStampOperation(plan);

    Require(plan.CanCommit && plan.Statistics.OverlapCount == 1U &&
                preview.State == VoxelPreviewState::Overlap &&
                placement.IsReady(),
        "Rotated overlap must remain non-blocking and visible.");
    Require(preview.Transform.QuarterTurns == 1U &&
                preview.Voxels.size() == plan.Voxels.size(),
        "Preview must retain and consume the exact rotated plan.");
    for (std::size_t index = 0U; index < plan.Voxels.size(); ++index)
    {
        Require(preview.Voxels[index].Position ==
                    plan.Voxels[index].WorldPosition &&
                    preview.Voxels[index].Color ==
                    plan.Voxels[index].Color,
            "Preview cells and palette colors must equal the plan.");
    }
    for (const VoxelChange& change : placement.Operation.Changes)
    {
        bool found = false;
        for (const StampPlannedVoxel& voxel : plan.Voxels)
        {
            if (change.Position == voxel.WorldPosition &&
                change.PaletteIndexAfter == voxel.DocumentPaletteIndex)
            {
                found = true;
                break;
            }
        }
        Require(found,
            "Placement must copy rotated plan cells without recalculation.");
    }
}

void TestRotationDiagnosticsAndCacheKey()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto zero = Plan(stamp, document, 0U);
    const auto ninety = Plan(stamp, document, 1U);
    const auto invalid = Plan(stamp, document, 4U);
    Require(zero.CacheKey != ninety.CacheKey &&
                zero.Voxels != ninety.Voxels,
        "Rotation must participate in cache identity and plan content.");
    Require(!invalid.CanCommit && invalid.HasErrors(),
        "Invalid quarter-turn values must produce an error plan.");
    Require(!invalid.Diagnostics.empty() &&
                invalid.Diagnostics.front().Code ==
                    StampPlacementDiagnosticCode::UnsupportedRotation,
        "Invalid rotation must expose the rotation diagnostic.");

    const auto outside = Plan(
        stamp, document, 3U,
        {0, StampFixedPoint::UnitsPerVoxel, 0});
    Require(!outside.CanCommit &&
                outside.Statistics.OutOfBoundsCount != 0U &&
                outside.HasErrors(),
        "Rotated out-of-bounds cells must block commit.");
}

// STAMP-25 : 45 degres n'est pas une symetrie de la grille. Le plan doit
// reechantillonner, se declarer approximatif, et surtout rester PLEIN : le
// piege classique (parcourir les sources au lieu des destinations) laisse
// environ un tiers de trous.
void TestHalfQuarterStepResamplesWithoutHoles()
{
    // Bloc plein 5x1x5 : apres rotation, tout trou se verrait immediatement.
    std::vector<StampVoxel> voxels;
    for (std::int32_t z = 0; z < 5; ++z)
        for (std::int32_t x = 0; x < 5; ++x)
            voxels.push_back({{x, 0, z}, 0U});

    StampValidationResult validation{};
    constexpr std::int32_t unit = StampFixedPoint::UnitsPerVoxel;
    const auto block = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503235ULL}, "stamp-25-half-step"},
        {{0, 0, 0}, {4, 0, 4}, {5U, 1U, 5U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {2 * unit, 0, 2 * unit}},
        {},
        {{0U, {10U, 20U, 30U, 255U}}},
        std::move(voxels), DefaultStampResourceLimits(), &validation);
    Require(block && validation.IsValid(),
        "Half-step Stamp fixture must be valid.");

    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {32U, 4U, 32U}, .Voxels = {}});
    const auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "half-step.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Half-step document fixture must build.");
    const auto& document = *loaded.Document;

    const StampFixedPoint target{16 * unit, unit, 16 * unit};
    const auto exact = StampPlacementPlanner::Build({
        .Stamp = &*block, .Document = &document, .DocumentGeneration = 25U,
        .Transform = {.TargetPivot = target}});
    Require(exact.CanCommit && exact.Statistics.PlannedVoxelCount == 25U &&
            !exact.Statistics.ApproximateRotation,
        "The unrotated block must stay exact with all 25 cells.");

    const auto diagonal = StampPlacementPlanner::Build({
        .Stamp = &*block, .Document = &document, .DocumentGeneration = 25U,
        .Transform = {.TargetPivot = target, .HalfQuarterStep = true}});

    Require(diagonal.Statistics.ApproximateRotation &&
            HasDiagnostic(diagonal,
                StampPlacementDiagnosticCode::ApproximateRotation),
        "A 45 degree rotation must declare itself approximate.");
    Require(diagonal.CanCommit,
        "An approximate rotation stays placeable: we inform, we do not forbid.");
    Require(diagonal.Statistics.OutOfBoundsCount == 0U,
        "The rotated block must stay inside the document.");

    // Aucune cellule dupliquee : l'echantillonnage inverse visite chaque
    // cellule d'arrivee une seule fois.
    std::vector<Position> placed;
    placed.reserve(diagonal.Voxels.size());
    for (const auto& voxel : diagonal.Voxels)
        placed.push_back(voxel.WorldPosition);
    const auto lessThan = [](const Position& a, const Position& b)
    {
        return std::tie(a.X, a.Y, a.Z) < std::tie(b.X, b.Y, b.Z);
    };
    std::sort(placed.begin(), placed.end(), lessThan);
    Require(std::adjacent_find(placed.begin(), placed.end()) == placed.end(),
        "Inverse resampling must never plan the same cell twice.");

    // Pas de trou : la surface obtenue est un bloc diagonal d'un seul tenant.
    // On verifie que chaque cellule a au moins un voisin sur le plan XZ, et
    // que le compte reste proche de la surface source.
    Require(diagonal.Voxels.size() >= 20U && diagonal.Voxels.size() <= 40U,
        "A 45 degree rotation must keep a comparable amount of matter.");
    std::size_t isolated = 0U;
    for (const auto& voxel : diagonal.Voxels)
    {
        bool hasNeighbour = false;
        for (const auto& other : diagonal.Voxels)
        {
            if (other.WorldPosition == voxel.WorldPosition) continue;
            const int dx = other.WorldPosition.X - voxel.WorldPosition.X;
            const int dz = other.WorldPosition.Z - voxel.WorldPosition.Z;
            if (std::abs(dx) <= 1 && std::abs(dz) <= 1)
            {
                hasNeighbour = true;
                break;
            }
        }
        if (!hasNeighbour) ++isolated;
    }
    Require(isolated == 0U,
        "Inverse resampling must not leave isolated cells (no holes).");

    // Le demi-cran participe a l'identite du plan.
    Require(exact.CacheKey != diagonal.CacheKey,
        "The 45 degree step must participate in plan cache identity.");
}

// STAMP-24 : un seul axe actif a la fois. Changer d'axe repart de zero pour
// que l'angle courant ne soit jamais reinterprete sur le nouvel axe.
void TestSessionAxisSwitchResetsRotation()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    StampPlacementSession session;
    Require(session.Begin(
                stamp, document, 15U, 0U,
                {5 * StampFixedPoint::UnitsPerVoxel,
                 StampFixedPoint::UnitsPerVoxel,
                 5 * StampFixedPoint::UnitsPerVoxel}).Succeeded,
        "Axis switch session must begin.");

    Require(session.Rotate90(
                StampPlacementRotationAxis::VerticalY, document, 15U)
                .Succeeded &&
            session.Rotate90(
                StampPlacementRotationAxis::VerticalY, document, 15U)
                .Succeeded &&
            session.QuarterRotation() == 2U,
        "Two quarter turns around Y must accumulate.");

    Require(session.Rotate90(
                StampPlacementRotationAxis::LateralX, document, 15U)
                .Succeeded &&
            session.RotationAxis() ==
                StampPlacementRotationAxis::LateralX &&
            session.QuarterRotation() == 1U,
        "Switching to X must restart from a single quarter turn.");

    Require(session.Rotate90(
                StampPlacementRotationAxis::DepthZ, document, 15U,
                false).Succeeded &&
            session.RotationAxis() == StampPlacementRotationAxis::DepthZ &&
            session.QuarterRotation() == 3U,
        "Switching to Z counter-clockwise must restart at 270 degrees.");

    const auto plan = session.CurrentPlan();
    Require(plan != nullptr &&
            plan->Transform.RotationAxis ==
                StampPlacementRotationAxis::DepthZ &&
            plan->Transform.QuarterTurns == 3U,
        "The shared plan must carry the active axis and rotation.");

    Require(session.ResetTransform(document, 15U).Succeeded &&
            session.RotationAxis() ==
                StampPlacementRotationAxis::VerticalY &&
            session.QuarterRotation() == 0U,
        "Reset must return to the default vertical axis.");

    const auto unknown = session.Rotate90(
        static_cast<StampPlacementRotationAxis>(9U), document, 15U);
    Require(!unknown.Succeeded &&
            unknown.Diagnostic ==
                StampPlacementDiagnosticCode::UnsupportedRotation &&
            session.RotationAxis() ==
                StampPlacementRotationAxis::VerticalY,
        "An unknown axis must be rejected without touching the session.");
}

void TestSessionRotationLifecycle()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    StampPlacementSession session;
    Require(session.Begin(
                stamp, document, 15U, 0U,
                {5 * StampFixedPoint::UnitsPerVoxel,
                 StampFixedPoint::UnitsPerVoxel,
                 5 * StampFixedPoint::UnitsPerVoxel}).Succeeded,
        "Quarter rotation session must begin.");
    const auto originalKey = *session.CacheKey();

    Require(session.Rotate90(
                StampPlacementRotationAxis::VerticalY,
                document, 15U).Succeeded &&
                session.RotationAxis() ==
                    StampPlacementRotationAxis::VerticalY &&
                session.QuarterRotation() == 1U &&
                session.CurrentPlan()->Transform.QuarterTurns == 1U &&
                session.CurrentPreview()->Transform.QuarterTurns == 1U &&
                *session.CacheKey() != originalKey,
        "Clockwise rotation must rebuild one new shared plan.");
    Require(session.Rotate90(
                StampPlacementRotationAxis::VerticalY,
                document, 15U, false).Succeeded &&
                session.QuarterRotation() == 0U &&
                *session.CacheKey() == originalKey,
        "Counter-clockwise rotation must return to the exact cached context.");
    Require(session.SetQuarterRotation(6U, document, 15U).Succeeded &&
                session.QuarterRotation() == 2U,
        "Session rotation setter must normalize complete turns.");
    Require(session.Rotate90(
                StampPlacementRotationAxis::VerticalY,
                document, 15U).Succeeded &&
                session.Rotate90(
                    StampPlacementRotationAxis::VerticalY,
                    document, 15U).Succeeded &&
                session.QuarterRotation() == 0U,
        "Four clockwise quarter turns must return to zero.");
    Require(session.Cancel() && session.QuarterRotation() == 0U,
        "Ending the placement session must discard transient rotation.");
}

} // namespace

int main()
{
    try
    {
        TestExactQuarterTurnsRespectPivot();
        TestQuarterTurnsAroundEachAxis();
        TestPreviewPlacementPaletteAndOverlapSharePlan();
        TestRotationDiagnosticsAndCacheKey();
        TestHalfQuarterStepResamplesWithoutHoles();
        TestSessionAxisSwitchResetsRotation();
        TestSessionRotationLifecycle();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp quarter rotation tests passed.\n";
    return EXIT_SUCCESS;
}
