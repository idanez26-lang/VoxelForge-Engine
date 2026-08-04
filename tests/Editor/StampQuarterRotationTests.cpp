#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

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
        TestPreviewPlacementPaletteAndOverlapSharePlan();
        TestRotationDiagnosticsAndCacheKey();
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
