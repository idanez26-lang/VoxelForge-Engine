#include "Preview/VoxelPreview.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelStamps/Placement/PlaceVoxelStampOperation.h"
#include "VoxelStamps/Placement/StampPlacementPlanner.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
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

VoxelStamp MakeStamp(
    const std::uint64_t id = 99U,
    const StampFixedPoint pivot = {128, 0, 0})
{
    const StampBounds bounds{{}, {1, 0, 0}, {2U, 1U, 1U}};
    const StampPivot stampPivot{.RequestedMode = StampPivotMode::Center,
        .ResolvedMode = StampPivotMode::Center, .LocalPosition = pivot};
    const std::vector<StampPaletteEntry> palette{
        {0U, {255U, 0U, 0U, 255U}},
        {1U, {0U, 0U, 255U, 255U}}};
    const std::vector<StampVoxel> voxels{
        {{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}};
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = "preview-" + std::to_string(id)},
        bounds, stampPivot, {}, palette, voxels,
        DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value() && validation.IsValid(),
        "Preview fixture Stamp must be valid.");
    return *stamp;
}

VoxelStamp MakeLargeStamp()
{
    constexpr std::int32_t side = 23;
    std::vector<StampVoxel> voxels;
    voxels.reserve(static_cast<std::size_t>(side * side));
    for (std::int32_t y = 0; y < side; ++y)
    {
        for (std::int32_t x = 0; x < side; ++x)
        {
            voxels.push_back({{x, y, 0}, 0U});
        }
    }
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {Core::UUID{0x5354414d5031344cULL}, "stamp-14-large"},
        {{}, {side - 1, side - 1, 0},
         {static_cast<std::uint32_t>(side),
          static_cast<std::uint32_t>(side), 1U}},
        {.RequestedMode = StampPivotMode::Corner,
         .ResolvedMode = StampPivotMode::Corner,
         .LocalPosition = {}},
        {}, {{0U, {40U, 180U, 90U, 255U}}}, std::move(voxels),
        DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value() && validation.IsValid(),
        "Large preview fixture Stamp must be valid.");
    return *stamp;
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models.push_back({.Dimensions = {32U, 32U, 32U},
        .Voxels = {{.X = 10U, .Y = 2U, .Z = 4U, .ColorIndex = 1U}}});
    const auto built =
        Asset::Voxel::VoxDocumentLoader{}.Build(source, "preview.vox");
    Require(built.Succeeded() && built.Document,
        "Preview document fixture must build.");
    return std::move(*built.Document);
}

StampPlacementPlan BuildPlan(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampFixedPoint target = {
        10 * StampFixedPoint::UnitsPerVoxel + 128,
        2 * StampFixedPoint::UnitsPerVoxel,
        8 * StampFixedPoint::UnitsPerVoxel},
    const std::uint64_t generation = 1U,
    const std::size_t subModel = 0U,
    const StampCollisionPolicy collisionPolicy =
        StampCollisionPolicy::Overwrite)
{
    return StampPlacementPlanner::Build({
        .Stamp = &stamp,
        .Document = &document,
        .DocumentGeneration = generation,
        .TargetSubModel = subModel,
        .Transform = {.TargetPivot = target},
        .CollisionPolicy = collisionPolicy});
}

VoxelPreviewData BuildPreview(
    const VoxelStamp& stamp,
    const Asset::Voxel::VoxelDocument& document,
    const StampFixedPoint target = {
        10 * StampFixedPoint::UnitsPerVoxel + 128,
        2 * StampFixedPoint::UnitsPerVoxel,
        8 * StampFixedPoint::UnitsPerVoxel})
{
    return BuildStampPreview(
        BuildPlan(stamp, document, target));
}

void Test01InactiveByDefault()
{
    VoxelPreviewSession session;
    Require(session.Current() == nullptr && session.Revision() == 0U,
        "01: Preview session must be inactive by default.");
}

void Test02ValidActivationAndExactCells()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    VoxelPreviewSession session;
    const auto preview = BuildPreview(stamp, document);
    Require(session.Activate(preview) && session.Current()->IsActive(),
        "02: A valid plan must activate a preview session.");
    Require(preview.Voxels.size() == stamp.Voxels().size(),
        "02: Preview must contain exactly the planned voxel count.");
}

void Test03ColorsAndFractionalPivotAreCopied()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto preview = BuildPreview(stamp, document);
    Require(
        preview.Voxels[0].Color ==
                Asset::Vox::VoxColor{255U, 0U, 0U, 255U} &&
            preview.Voxels[1].Color ==
                Asset::Vox::VoxColor{0U, 0U, 255U, 255U},
        "03: Preview must preserve planned real colors.");
    Require(preview.Pivot.LocalPosition == VoxelPreviewFixedPoint{128, 0, 0},
        "03: Preview must preserve the exact fixed-point pivot.");
}

void Test04PreviewUsesExactPlannedWorldPositions()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    Require(preview.Voxels[0].Position == plan.Voxels[0].WorldPosition &&
                preview.Voxels[1].Position == plan.Voxels[1].WorldPosition,
        "04: Preview positions must be copied from the plan.");
}

void Test05OverlapIsNonBlocking()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const StampFixedPoint overlap{
        10 * StampFixedPoint::UnitsPerVoxel + 128,
        2 * StampFixedPoint::UnitsPerVoxel,
        4 * StampFixedPoint::UnitsPerVoxel};
    const auto plan = BuildPlan(stamp, document, overlap);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    Require(plan.CanCommit && plan.Statistics.OverlapCount == 1U &&
                preview.State == VoxelPreviewState::Overlap &&
                preview.Voxels[0].OverlapsExisting,
        "05: Overlap must remain a visible, non-blocking planned state.");
}

void Test06ClearAndCacheBehavior()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto preview = BuildPreview(stamp, document);
    VoxelPreviewSession session;
    Require(session.Activate(preview), "06: First preview must activate.");
    const std::uint64_t revision = session.Revision();
    Require(!session.Activate(preview) && session.Revision() == revision,
        "06: Identical plans must reuse the preview snapshot.");
    Require(session.Clear() && session.Current() == nullptr && !session.Clear(),
        "06: Clear must release the snapshot exactly once.");
}

void Test07PlanChangesRebuildPreview()
{
    const VoxelStamp first = MakeStamp(100U);
    const VoxelStamp second = MakeStamp(101U);
    const auto document = MakeDocument();
    VoxelPreviewSession session;
    static_cast<void>(session.Activate(BuildPreview(first, document)));
    const std::uint64_t firstRevision = session.Revision();
    Require(session.Activate(BuildPreview(second, document)) &&
                session.Revision() == firstRevision + 1U,
        "07: Changing Stamp identity must rebuild preview.");

    const auto moved = BuildPreview(second, document, {
        12 * StampFixedPoint::UnitsPerVoxel + 128,
        2 * StampFixedPoint::UnitsPerVoxel,
        8 * StampFixedPoint::UnitsPerVoxel});
    const std::uint64_t secondRevision = session.Revision();
    Require(session.Activate(moved) &&
                session.Revision() == secondRevision + 1U,
        "07: Changing the planned transform must rebuild preview.");
}

void Test08PreviewIsReadOnly()
{
    const VoxelStamp stamp = MakeStamp();
    auto document = MakeDocument();
    VoxelEditHistory history;
    const std::size_t voxelCount = document.GetVoxelCount();
    const std::uint64_t revision = document.GetRevision();
    static_cast<void>(BuildPreview(stamp, document));
    Require(document.GetVoxelCount() == voxelCount &&
                document.GetRevision() == revision &&
                history.UndoCount() == 0U && history.RedoCount() == 0U,
        "08: Plan adaptation must not mutate document or history.");
}

void Test09InvalidPlanFailsSafely()
{
    const StampPlacementPlan missing{};
    const auto preview = StampLivePreviewBuilder::Build(missing);
    Require(!preview.IsActive() &&
                preview.State == VoxelPreviewState::Invalid,
        "09: Missing plan data must fail safely.");
}

void Test10OutOfBoundsPlanIsInvalidPreview()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document, {
        -2 * StampFixedPoint::UnitsPerVoxel + 128, 0, 0});
    const auto preview = StampLivePreviewBuilder::Build(plan);
    Require(!plan.CanCommit &&
                plan.Statistics.OutOfBoundsCount != 0U &&
                preview.State == VoxelPreviewState::Invalid,
        "10: Out-of-bounds cells must be diagnosed by the plan.");
}

void Test11PreviewIsExcludedFromRayPicking()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    Require(BuildPreview(stamp, document).IsActive(),
        "11: Ray-picking fixture preview must exist.");
    const auto hit = RaycastVoxelDocument(
        document, {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
    Require(!hit.has_value(),
        "11: Document ray picking must not see preview-only cells.");
}

void Test12InvalidSubmodelFailsSafely()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document, {}, 1U, 1U);
    const auto preview = StampLivePreviewBuilder::Build(plan);
    Require(!plan.CanCommit && !preview.IsActive(),
        "12: Invalid submodels must not create partial previews.");
}

void Test13CommonContractPreservesExactPlan()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document);
    const auto preview = BuildStampPreview(plan);
    const auto instances = preview.Placement.Instances();
    Require(preview.IsActive() && preview.Placement.IsActive() &&
                instances.size() == plan.Voxels.size() &&
                preview.Placement.Statistics().Total ==
                    plan.Statistics.PlannedVoxelCount &&
                preview.Placement.Statistics().Valid ==
                    plan.Statistics.PlannedVoxelCount &&
                preview.WorldBounds.Minimum == plan.WorldBounds.Minimum &&
                preview.WorldBounds.Maximum == plan.WorldBounds.Maximum,
        "13: The common preview must retain exact plan counts and bounds.");
    for (std::size_t index = 0U; index < instances.size(); ++index)
    {
        Require(instances[index].Position ==
                    plan.Voxels[index].WorldPosition &&
                    instances[index].Semantic ==
                        VoxelPreviewSemantic::Valid,
            "13: The common preview must copy every planned world cell.");
    }
}

void Test14CommonContractShowsOrangeAndBlockingRedStates()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const StampFixedPoint overlapTarget{
        10 * StampFixedPoint::UnitsPerVoxel + 128,
        2 * StampFixedPoint::UnitsPerVoxel,
        4 * StampFixedPoint::UnitsPerVoxel};

    const auto overlapPlan = BuildPlan(stamp, document, overlapTarget);
    const auto overlap = BuildStampPreview(overlapPlan);
    Require(overlapPlan.CanCommit &&
                overlap.State == VoxelPreviewState::Overlap &&
                overlap.Placement.Statistics().Overlap == 1U &&
                overlap.Placement.Statistics().Valid == 1U &&
                overlap.Placement.Instances()[0].Semantic ==
                    VoxelPreviewSemantic::Overlap,
        "14: A non-blocking overlap must remain an orange common-preview state.");

    const auto rejectedPlan = BuildPlan(stamp, document, overlapTarget,
        1U, 0U, StampCollisionPolicy::Reject);
    const auto rejected = BuildStampPreview(rejectedPlan);
    Require(!rejectedPlan.CanCommit && rejectedPlan.HasErrors() &&
                rejected.State == VoxelPreviewState::Invalid &&
                rejected.Placement.Statistics().Invalid ==
                    rejectedPlan.Statistics.PlannedVoxelCount &&
                rejected.Placement.Statistics().Overlap == 0U,
        "14: A blocking collision must turn the complete common preview red.");
}

void Test15UnchangedPlanReusesTheActiveSnapshot()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document);
    VoxelPreviewSession session;
    Require(session.Activate(BuildStampPreview(plan)),
        "15: The first immutable plan must activate its preview.");
    const std::uint64_t revision = session.Revision();
    const VoxelPreviewInstance* const storage =
        session.Current()->Placement.Instances().data();
    Require(!session.Activate(BuildStampPreview(plan)) &&
                session.Revision() == revision &&
                session.Current()->Placement.Instances().data() == storage,
        "15: Reusing an unchanged plan must retain revision and immutable storage.");
}

void Test16PreviewAndOperationUseTheSamePlanPositions()
{
    const VoxelStamp stamp = MakeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document);
    const auto preview = BuildStampPreview(plan);
    const auto prepared = PreparePlaceVoxelStampOperation(plan);
    Require(prepared.IsReady() &&
                prepared.Operation.Changes.size() ==
                    preview.Placement.Instances().size(),
        "16: A valid preview and operation must consume the same complete plan.");
    for (const VoxelPreviewInstance& instance :
         preview.Placement.Instances())
    {
        bool found = false;
        for (const VoxelChange& change : prepared.Operation.Changes)
        {
            if (change.Position == instance.Position)
            {
                found = true;
                break;
            }
        }
        Require(found,
            "16: Every exact preview position must exist in the prepared operation.");
    }
}

void Test17LargePreviewUsesTheCommonAggregatePolicy()
{
    const VoxelStamp stamp = MakeLargeStamp();
    const auto document = MakeDocument();
    const auto plan = BuildPlan(stamp, document, {});
    const auto preview = BuildStampPreview(plan);
    Require(plan.CanCommit && preview.IsActive() &&
                preview.Voxels.size() == 529U &&
                preview.Placement.Statistics().Total == 529U &&
                preview.Placement.Instances().size() == 529U &&
                preview.Placement.InstancesComplete() &&
                preview.Placement.RenderMode() ==
                    VoxelPreviewRenderMode::AggregateBounds,
        "17: A large exact Stamp preview must select the common aggregate render policy.");
}

} // namespace

int main()
{
    try
    {
        Test01InactiveByDefault();
        Test02ValidActivationAndExactCells();
        Test03ColorsAndFractionalPivotAreCopied();
        Test04PreviewUsesExactPlannedWorldPositions();
        Test05OverlapIsNonBlocking();
        Test06ClearAndCacheBehavior();
        Test07PlanChangesRebuildPreview();
        Test08PreviewIsReadOnly();
        Test09InvalidPlanFailsSafely();
        Test10OutOfBoundsPlanIsInvalidPreview();
        Test11PreviewIsExcludedFromRayPicking();
        Test12InvalidSubmodelFailsSafely();
        Test13CommonContractPreservesExactPlan();
        Test14CommonContractShowsOrangeAndBlockingRedStates();
        Test15UnchangedPlanReusesTheActiveSnapshot();
        Test16PreviewAndOperationUseTheSamePlanPositions();
        Test17LargePreviewUsesTheCommonAggregatePolicy();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp Live Preview tests passed.\n";
    return EXIT_SUCCESS;
}
