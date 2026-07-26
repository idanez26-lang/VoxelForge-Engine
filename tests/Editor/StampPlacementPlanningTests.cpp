#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

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
        .Dimensions = {16U, 8U, 8U},
        .Voxels = {
            {.X = 4U, .Y = 1U, .Z = 1U, .ColorIndex = 1U}}});
    const auto built =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "planning.vox");
    Require(built.Succeeded() && built.Document,
        "Planning document fixture must build.");
    return std::move(*built.Document);
}

VoxelStamp MakeStamp()
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d503134ULL}, "stamp-14-planning"},
        {{0, 0, 0}, {2, 0, 0}, {3U, 1U, 1U}},
        {.RequestedMode = StampPivotMode::Center,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {0, 0, 0}},
        {},
        {{0U, {11U, 22U, 33U, 255U}},
         {1U, {44U, 55U, 66U, 255U}}},
        {{{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}, {{2, 0, 0}, 0U}},
        DefaultStampResourceLimits(), &validation);
    Require(stamp && validation.IsValid(),
        "Planning Stamp fixture must be valid.");
    return *stamp;
}

StampPlacementPlan Plan(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampFixedPoint target = {
        3 * StampFixedPoint::UnitsPerVoxel,
        StampFixedPoint::UnitsPerVoxel,
        StampFixedPoint::UnitsPerVoxel},
    const std::uint64_t generation = 7U)
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = generation,
        .Transform = {.TargetPivot = target}});
}

bool HasDiagnostic(
    const StampPlacementPlan& plan,
    const StampPlacementDiagnosticCode code)
{
    for (const StampPlacementDiagnostic& diagnostic : plan.Diagnostics)
    {
        if (diagnostic.Code == code) return true;
    }
    return false;
}

void TestEmptyAndInvalidPlanning()
{
    const auto missing = StampPlacementPlanner::Build({});
    Require(!missing.CanCommit &&
                HasDiagnostic(missing,
                    StampPlacementDiagnosticCode::MissingStamp),
        "Missing Stamp must produce an explicit error.");

    const VoxelStamp stamp = MakeStamp();
    const auto noDocument =
        StampPlacementPlanner::Build({.Stamp = &stamp});
    Require(!noDocument.CanCommit &&
                HasDiagnostic(noDocument,
                    StampPlacementDiagnosticCode::MissingDocument),
        "Missing document must produce an explicit error.");

    const auto document = MakeDocument();
    const auto noGeneration = StampPlacementPlanner::Build({
        .Stamp = &stamp, .Document = &document});
    Require(!noGeneration.CanCommit &&
                HasDiagnostic(noGeneration,
                    StampPlacementDiagnosticCode::
                        InvalidDocumentGeneration),
        "Invalid generation must produce an explicit error.");
}

void TestValidPlanIsCompleteAndDeterministic()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto first = Plan(stamp, document);
    const auto second = Plan(stamp, document);
    if (!first.CanCommit)
    {
        std::cerr << "Planning diagnostic: palette="
                  << static_cast<int>(first.PaletteStatus)
                  << ", changed=" << first.Statistics.ChangedVoxelCount
                  << ", oob=" << first.Statistics.OutOfBoundsCount << '\n';
        for (const auto& diagnostic : first.Diagnostics)
        {
            std::cerr << "  "
                      << static_cast<int>(diagnostic.Code)
                      << ": " << diagnostic.Message() << '\n';
        }
    }
    Require(first.CanCommit && !first.HasErrors(),
        "Valid input must produce a committable plan.");
    Require(first.Stamp == stamp.Identity() &&
                first.DocumentGeneration == 7U &&
                first.DocumentRevision == document.GetRevision() &&
                first.TargetSubModel == 0U,
        "Plan must retain all input identities.");
    Require(first.Voxels.size() == 3U &&
                first.Statistics.TotalVoxelCount == 3U &&
                first.Statistics.PlannedVoxelCount == 3U &&
                first.WorldBounds.Valid,
        "Plan must contain exact cells, statistics, and bounds.");
    Require(first.CacheKey == second.CacheKey &&
                first.Voxels == second.Voxels &&
                first.PaletteMapping == second.PaletteMapping,
        "Same context must produce a deterministic plan.");
}

void TestPreviewAndPlacementArePurePlanAdapters()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = Plan(stamp, document);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    const auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(preview.IsActive() &&
                preview.Voxels.size() == plan.Voxels.size() &&
                prepared.IsReady(),
        "Both adapters must accept the same valid plan.");
    Require(prepared.Operation.Changes.size() ==
                plan.Statistics.ChangedVoxelCount,
        "Placement changes must equal planned changed cells.");
    for (std::size_t index = 0U; index < plan.Voxels.size(); ++index)
    {
        Require(preview.Voxels[index].Position ==
                    plan.Voxels[index].WorldPosition &&
                    preview.Voxels[index].Color ==
                    plan.Voxels[index].Color,
            "Preview must copy exact planned world cells.");
    }
    for (const VoxelChange& change : prepared.Operation.Changes)
    {
        bool found = false;
        for (const StampPlannedVoxel& voxel : plan.Voxels)
        {
            if (change.Position == voxel.WorldPosition &&
                change.PaletteIndexAfter ==
                    voxel.FinalVoxel.PaletteIndex)
            {
                found = true;
                break;
            }
        }
        Require(found,
            "Every operation change must originate from the same plan.");
    }
}

void TestOverlapAndBoundsDiagnostics()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto overlap = Plan(stamp, document);
    Require(overlap.CanCommit &&
                overlap.Statistics.OverlapCount == 1U &&
                overlap.Voxels[1].Overlap,
        "Overwrite overlap must be visible and non-blocking.");

    const auto outOfBounds = Plan(stamp, document,
        {-StampFixedPoint::UnitsPerVoxel, 0, 0});
    Require(!outOfBounds.CanCommit &&
                outOfBounds.Statistics.OutOfBoundsCount != 0U &&
                HasDiagnostic(outOfBounds,
                    StampPlacementDiagnosticCode::OutOfBounds),
        "Out-of-bounds cells must block commit with diagnostics.");
}

void TestUnsupportedFutureTransformsAreExplicit()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto rotation = StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 7U,
        .Transform = {.TargetPivot = {}, .QuarterTurns = 4U}});
    Require(!rotation.CanCommit &&
                HasDiagnostic(rotation,
                    StampPlacementDiagnosticCode::UnsupportedRotation),
        "Non-quarter rotation states must fail explicitly.");

    const auto mirror = StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = 7U,
        .Transform = {.TargetPivot = {},
                      .Mirror = {.X = true}}});
    Require(!mirror.CanCommit &&
                HasDiagnostic(mirror,
                    StampPlacementDiagnosticCode::UnsupportedMirror),
        "Reserved mirror must fail explicitly in V1.");
}

void TestStalePlansAreDetectedByRevisionAndGeneration()
{
    const VoxelStamp stamp = MakeStamp();
    auto document = MakeDocument();
    const auto plan = Plan(stamp, document);
    Require(plan.IsCurrent(document, 7U, 0U),
        "Fresh plan must match its document context.");
    Require(!plan.IsCurrent(document, 8U, 0U),
        "Generation change must stale the plan.");
    const auto otherDocument = MakeDocument();
    Require(!plan.IsCurrent(otherDocument, 7U, 0U),
        "A different document identity must stale the plan.");
    Require(document.SetVoxel({8, 0, 0}, 1U).Succeeded,
        "Unable to mutate stale-plan fixture.");
    Require(!plan.IsCurrent(document, 7U, 0U),
        "Document revision change must stale the plan.");
}

void TestSessionLifecycleAndCache()
{
    const VoxelStamp stamp = MakeStamp();
    auto document = MakeDocument();
    StampPlacementSession session;
    Require(session.State() == StampPlacementSessionState::Empty &&
                !session.IsActive() &&
                session.CurrentPlan() == nullptr,
        "Session must start empty.");
    const auto begun = session.Begin(
        stamp, document, 7U, 0U,
        {3 * StampFixedPoint::UnitsPerVoxel,
         StampFixedPoint::UnitsPerVoxel,
         StampFixedPoint::UnitsPerVoxel});
    Require(begun.Succeeded && begun.PlanChanged &&
                begun.PreviewChanged && session.IsActive() &&
                session.IsCurrent(document, 7U) &&
                session.CacheKey() != nullptr,
        "Begin must atomically build plan, cache, and preview.");
    const auto unchanged = session.Rebuild(document, 7U);
    Require(unchanged.Succeeded && !unchanged.PlanChanged &&
                !unchanged.PreviewChanged,
        "Unchanged context must reuse the cached plan and preview.");
    const auto moved = session.TranslateTarget(2, 0, 0, document, 7U);
    Require(moved.Succeeded && moved.PlanChanged &&
                moved.PreviewChanged &&
                session.Target().X ==
                    5 * StampFixedPoint::UnitsPerVoxel,
        "Target change must rebuild one new plan.");
    session.MarkPlacementCommitted();
    Require(session.PlacementOrdinal() == 1U,
        "Placement ordinal must advance only on commit.");
    Require(session.Cancel() && !session.IsActive() &&
                session.State() == StampPlacementSessionState::Cancelled &&
                session.CurrentPlan() == nullptr &&
                session.CurrentPreview() == nullptr,
        "Cancel must release all active plan and preview data.");
}

void TestStaleSessionRebuildRequiresAnotherCommitAttempt()
{
    const VoxelStamp stamp = MakeStamp();
    auto document = MakeDocument();
    StampPlacementSession session;
    Require(session.Begin(stamp, document, 7U, 0U,
                {3 * StampFixedPoint::UnitsPerVoxel,
                 StampFixedPoint::UnitsPerVoxel,
                 StampFixedPoint::UnitsPerVoxel}).Succeeded,
        "Unable to begin stale-session fixture.");
    const std::uint64_t ordinal = session.PlacementOrdinal();
    Require(document.SetVoxel({10, 0, 0}, 1U).Succeeded,
        "Unable to mutate session document.");
    Require(!session.IsCurrent(document, 7U),
        "External document mutation must stale the active plan.");
    const auto rebuilt = session.Rebuild(document, 7U);
    Require(rebuilt.Succeeded && rebuilt.PlanChanged &&
                session.IsCurrent(document, 7U) &&
                session.PlacementOrdinal() == ordinal,
        "Stale click path must rebuild without committing or advancing.");
}

} // namespace

int main()
{
    try
    {
        TestEmptyAndInvalidPlanning();
        TestValidPlanIsCompleteAndDeterministic();
        TestPreviewAndPlacementArePurePlanAdapters();
        TestOverlapAndBoundsDiagnostics();
        TestUnsupportedFutureTransformsAreExplicit();
        TestStalePlansAreDetectedByRevisionAndGeneration();
        TestSessionLifecycleAndCache();
        TestStaleSessionRebuildRequiresAnotherCommitAttempt();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp placement planning tests passed.\n";
    return EXIT_SUCCESS;
}
