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
        .Voxels = {{.X = 6U, .Y = 1U, .Z = 4U, .ColorIndex = 1U}}});
    const auto loaded =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "mirror.vox");
    Require(loaded.Succeeded() && loaded.Document,
        "Mirror document fixture must build.");
    return std::move(*loaded.Document);
}

VoxelStamp MakeStamp()
{
    constexpr std::int32_t unit = StampFixedPoint::UnitsPerVoxel;
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503137ULL}, "stamp-17-mirror"},
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
        "Mirror Stamp fixture must be valid.");
    return *stamp;
}

StampPlacementPlan Plan(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampPlacementMirrorMode mirror,
    const std::uint8_t quarterTurns = 0U,
    const StampFixedPoint target = {
        5 * StampFixedPoint::UnitsPerVoxel,
        StampFixedPoint::UnitsPerVoxel,
        5 * StampFixedPoint::UnitsPerVoxel})
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 17U,
        .Transform = {
            .TargetPivot = target,
            .QuarterTurns = quarterTurns,
            .Mirror = mirror}});
}

void RequirePositions(
    const StampPlacementPlan& plan,
    const std::array<Position, 3U>& expected)
{
    Require(plan.Voxels.size() == expected.size(),
        "Mirror must preserve the exact voxel count.");
    for (std::size_t index = 0U; index < expected.size(); ++index)
    {
        Require(plan.Voxels[index].WorldPosition == expected[index],
            "Mirror produced an unexpected exact grid cell.");
    }
}

void TestMirrorModesRespectPivot()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();

    const auto none =
        Plan(stamp, document, StampPlacementMirrorMode::None);
    const auto mirrorX =
        Plan(stamp, document, StampPlacementMirrorMode::X);
    const auto mirrorZ =
        Plan(stamp, document, StampPlacementMirrorMode::Z);
    const auto mirrorXZ =
        Plan(stamp, document, StampPlacementMirrorMode::XZ);

    RequirePositions(none, {{{4, 1, 4}, {6, 1, 4}, {4, 1, 5}}});
    RequirePositions(mirrorX, {{{6, 1, 4}, {4, 1, 4}, {6, 1, 5}}});
    RequirePositions(mirrorZ, {{{4, 1, 6}, {6, 1, 6}, {4, 1, 5}}});
    RequirePositions(mirrorXZ, {{{6, 1, 6}, {4, 1, 6}, {6, 1, 5}}});

    Require(none.Transform.Mirror == StampPlacementMirrorMode::None &&
                mirrorX.Transform.Mirror == StampPlacementMirrorMode::X &&
                mirrorZ.Transform.Mirror == StampPlacementMirrorMode::Z &&
                mirrorXZ.Transform.Mirror == StampPlacementMirrorMode::XZ,
        "Plan metadata must retain the exact mirror mode.");
    Require(mirrorX.Statistics.OverlapCount == 1U,
        "Mirrored overlap must be derived from final planned cells.");
}

void TestMirrorIsAppliedBeforeEveryQuarterRotation()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();

    const auto zero =
        Plan(stamp, document, StampPlacementMirrorMode::XZ, 0U);
    const auto ninety =
        Plan(stamp, document, StampPlacementMirrorMode::XZ, 1U);
    const auto oneEighty =
        Plan(stamp, document, StampPlacementMirrorMode::XZ, 2U);
    const auto twoSeventy =
        Plan(stamp, document, StampPlacementMirrorMode::XZ, 3U);

    RequirePositions(zero, {{{6, 1, 6}, {4, 1, 6}, {6, 1, 5}}});
    RequirePositions(ninety, {{{6, 1, 4}, {6, 1, 6}, {5, 1, 4}}});
    RequirePositions(oneEighty, {{{4, 1, 4}, {6, 1, 4}, {4, 1, 5}}});
    RequirePositions(twoSeventy, {{{4, 1, 6}, {4, 1, 4}, {5, 1, 6}}});

    Require(zero.WorldBounds != ninety.WorldBounds &&
                ninety.WorldBounds != oneEighty.WorldBounds,
        "Bounds must be derived after mirror and rotation.");
}

void TestPreviewPlacementPaletteAndCacheSharePlan()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto none =
        Plan(stamp, document, StampPlacementMirrorMode::None, 1U);
    const auto mirrored =
        Plan(stamp, document, StampPlacementMirrorMode::X, 1U);
    const auto preview = StampLivePreviewBuilder::Build(mirrored);
    const auto placement = PreparePlaceVoxelStampOperation(mirrored);

    Require(none.CacheKey != mirrored.CacheKey &&
                none.Voxels != mirrored.Voxels,
        "Mirror must participate in cache identity and planned cells.");
    Require(preview.Transform.MirrorMode ==
                static_cast<std::uint8_t>(StampPlacementMirrorMode::X) &&
                preview.Transform.QuarterTurns == 1U &&
                placement.IsReady(),
        "Preview metadata and placement must consume the mirrored plan.");
    for (std::size_t index = 0U; index < mirrored.Voxels.size(); ++index)
    {
        Require(preview.Voxels[index].Position ==
                    mirrored.Voxels[index].WorldPosition &&
                    preview.Voxels[index].Color ==
                    mirrored.Voxels[index].Color,
            "Preview must copy mirrored plan cells and palette colors.");
    }
    for (const VoxelChange& change : placement.Operation.Changes)
    {
        bool found = false;
        for (const StampPlannedVoxel& voxel : mirrored.Voxels)
        {
            if (change.Position == voxel.WorldPosition &&
                change.PaletteIndexAfter == voxel.DocumentPaletteIndex)
            {
                found = true;
                break;
            }
        }
        Require(found,
            "Placement must copy mirrored plan cells without recalculation.");
    }
}

void TestMirrorDiagnosticsAndOutOfBounds()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto invalid = Plan(
        stamp, document,
        static_cast<StampPlacementMirrorMode>(255U));
    Require(!invalid.CanCommit && invalid.HasErrors() &&
                !invalid.Diagnostics.empty() &&
                invalid.Diagnostics.front().Code ==
                    StampPlacementDiagnosticCode::UnsupportedMirror,
        "Unknown mirror values must produce an explicit error plan.");

    const auto outside = Plan(
        stamp, document, StampPlacementMirrorMode::X, 3U,
        {0, StampFixedPoint::UnitsPerVoxel, 0});
    Require(!outside.CanCommit &&
                outside.Statistics.OutOfBoundsCount != 0U &&
                outside.HasErrors(),
        "Mirrored and rotated out-of-bounds cells must block commit.");
}

void TestSessionMirrorLifecycle()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    StampPlacementSession session;
    Require(session.Begin(
                stamp, document, 17U, 0U,
                {5 * StampFixedPoint::UnitsPerVoxel,
                 StampFixedPoint::UnitsPerVoxel,
                 5 * StampFixedPoint::UnitsPerVoxel}).Succeeded,
        "Mirror placement session must begin.");
    const auto originalKey = *session.CacheKey();

    Require(session.ToggleMirror(
                StampPlacementMirrorMode::X, document, 17U).Succeeded &&
                session.Mirror() == StampPlacementMirrorMode::X &&
                session.CurrentPlan()->Transform.Mirror ==
                    StampPlacementMirrorMode::X &&
                *session.CacheKey() != originalKey,
        "ToggleMirror X must rebuild one shared plan.");
    Require(session.ToggleMirror(
                StampPlacementMirrorMode::Z, document, 17U).Succeeded &&
                session.Mirror() == StampPlacementMirrorMode::XZ,
        "ToggleMirror Z must combine with X as XZ.");
    Require(session.ToggleMirror(
                StampPlacementMirrorMode::X, document, 17U).Succeeded &&
                session.Mirror() == StampPlacementMirrorMode::Z,
        "Toggling X again must preserve only Z.");
    Require(session.ToggleMirror(
                StampPlacementMirrorMode::Z, document, 17U).Succeeded &&
                session.Mirror() == StampPlacementMirrorMode::None &&
                *session.CacheKey() == originalKey,
        "Toggling Z again must return to the neutral cache context.");
    Require(session.CycleMirror(document, 17U).Succeeded &&
                session.Mirror() == StampPlacementMirrorMode::X,
        "CycleMirror must remain available as the quick cycle command.");
    Require(session.Cancel() &&
                session.Mirror() == StampPlacementMirrorMode::None,
        "Ending the placement session must discard transient mirror state.");
}

} // namespace

int main()
{
    try
    {
        TestMirrorModesRespectPivot();
        TestMirrorIsAppliedBeforeEveryQuarterRotation();
        TestPreviewPlacementPaletteAndCacheSharePlan();
        TestMirrorDiagnosticsAndOutOfBounds();
        TestSessionMirrorLifecycle();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp mirror tests passed.\n";
    return EXIT_SUCCESS;
}
