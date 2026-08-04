#include "SmartTools/SmartPreviewEngine.h"
#include "SmartTools/SmartToolController.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

struct PositionHash final
{
    [[nodiscard]] std::size_t operator()(const Position position) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(position.X)) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Y)) << 11U) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(position.Z)) << 22U);
    }
};
using States = std::unordered_map<Position, SmartToolVoxelState, PositionHash>;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

SmartToolRequest Request(const SmartAction action, const States& states,
    const Position target = {2, 2, 2}, const int size = 1)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Action = action;
    request.BrushRequest.Dimensions = {8U, 8U, 8U};
    request.BrushRequest.State.Shape = SmartBrushShape::Cube;
    request.BrushRequest.State.Size = size;
    request.BrushRequest.State.PaletteIndex = 7U;
    request.BrushRequest.Placement = {target, {0, 1, 0}};
    const auto snapshot = std::make_shared<States>(states);
    request.ReadVoxel = [snapshot](const Position position)
    {
        const auto found = snapshot->find(position);
        return found == snapshot->end() ? SmartToolVoxelState{} : found->second;
    };
    request.SourceIdentity = 0x303U;
    request.SourceRevision = 17U;
    request.SourceGeneration = 2U;
    request.PreviewAlpha = 0.65F;
    request.HasPaletteColors = true;
    request.PaletteColors[3U] = {0.12F, 0.22F, 0.74F, 1.0F};
    request.PaletteColors[7U] = {0.84F, 0.18F, 0.36F, 1.0F};
    return request;
}

SmartToolPlanPtr Plan(const SmartToolRequest& request)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Unable to produce a Smart Tool plan.");
    return result.Plan;
}

void TestEmptyAndExactGhosts()
{
    auto empty = Request(SmartAction::Add, States{});
    empty.BrushRequest.State.Size = 0;
    const SmartPreviewData noPreview = SmartPreviewEngine::Build(*Plan(empty));
    Require(noPreview.GhostVoxels.empty() && !noPreview.Bounds.HasValue &&
            !noPreview.CanCommit(),
        "An empty plan produced a synthetic preview ghost or commit state.");

    const SmartPreviewData preview = SmartPreviewEngine::Build(
        *Plan(Request(SmartAction::Add, States{})));
    Require(preview.GhostVoxels.size() == 1U && preview.AffectedPositions.size() == 1U &&
            preview.GhostVoxels.front().Position == Position{2, 2, 2} &&
            preview.GhostVoxels.front().State == GhostVoxelState::Added &&
            preview.GhostVoxels.front().Alpha == 1.0F &&
            preview.Bounds.HasValue && preview.Bounds.Dimensions == Asset::Voxel::VoxelDimensions{1U, 1U, 1U} &&
            preview.CanCommit(),
        "The preview did not reproduce the exact changed plan cell.");
    Require(preview.GhostVoxels.front().Color ==
            std::array<float, 4>{0.84F, 0.18F, 0.36F, 1.0F},
        "The preview did not expose the exact final palette colour.");
    const auto common = preview.Placement.Instances();
    Require(common.size() == preview.GhostVoxels.size() &&
            common.front().Position == preview.GhostVoxels.front().Position &&
            common.front().Semantic == VoxelPreviewSemantic::Added &&
            common.front().Color == preview.GhostVoxels.front().Color &&
            common.front().Alpha == preview.GhostVoxels.front().Alpha &&
            preview.Placement.Statistics().Added == 1U,
        "Smart Preview and the common renderer contract diverged.");
}

void TestDiagnosticsAndColours()
{
    const States occupied{{{2, 2, 2}, {true, 3U}}};
    const SmartPreviewData overlap = SmartPreviewEngine::Build(
        *Plan(Request(SmartAction::Add, occupied)));
    Require(overlap.Diagnostics.HasOverlap && overlap.Diagnostics.HasNoChange &&
            !overlap.CanCommit() && overlap.GhostVoxels.front().State == GhostVoxelState::Ignored,
        "Overlap/no-change diagnostics were not copied from the plan.");

    const SmartPreviewData paint = SmartPreviewEngine::Build(
        *Plan(Request(SmartAction::Paint, occupied)));
    Require(paint.GhostVoxels.front().State == GhostVoxelState::Painted &&
            paint.GhostVoxels.front().Color[0] > 0.50F && paint.CanCommit(),
        "Paint preview did not use the plan's final palette colour.");

    const SmartPreviewData clipped = SmartPreviewEngine::Build(
        *Plan(Request(SmartAction::Add, States{}, {-1, 2, 2})));
    Require(clipped.Diagnostics.HasOutOfBounds && !clipped.GhostVoxels.empty() &&
            clipped.GhostVoxels.front().State == GhostVoxelState::Clipped &&
            clipped.Bounds.HasValue && clipped.Bounds.Minimum ==
                clipped.GhostVoxels.front().Position && clipped.Bounds.Maximum ==
                clipped.GhostVoxels.front().Position,
        "Out-of-bounds diagnostics, ghosts, and their bounds diverged.");
}

void TestCubeAndCache()
{
    const SmartToolPlanPtr cube = Plan(Request(SmartAction::Add, States{}, {3, 3, 3}, 3));
    SmartPreviewCache cache;
    const SmartPreviewData* first = &cache.Resolve(cube);
    const SmartPreviewData* repeated = &cache.Resolve(cube);
    Require(first == repeated && cache.BuildCount() == 1U &&
            first->GhostVoxels.size() == cube->Cells().size() &&
            first->AffectedPositions.size() == cube->Statistics().Changed,
        "Preview cache rebuilt an unchanged immutable plan or omitted a planned voxel.");
    const SmartToolPlanPtr changed = Plan(Request(SmartAction::Add, States{}, {4, 3, 3}, 3));
    static_cast<void>(cache.Resolve(changed));
    Require(cache.BuildCount() == 2U,
        "Preview cache did not invalidate for a new immutable plan.");

    SmartToolRequest alphaChanged = Request(SmartAction::Add, States{}, {4, 3, 3}, 3);
    alphaChanged.PreviewAlpha = 0.25F;
    const SmartToolPlanPtr alphaPlan = Plan(alphaChanged);
    const SmartPreviewData& alphaPreview = cache.Resolve(alphaPlan);
    Require(cache.BuildCount() == 3U && alphaPreview.GhostVoxels.front().Alpha == 1.0F,
        "Exact preview opacity must not be affected by the legacy ghost alpha setting.");
}

void TestPaletteSnapshotInvalidatesSessionPlan()
{
    SmartToolController controller;
    SmartToolSession session;
    SmartToolRequest firstRequest = Request(SmartAction::Add, States{});
    const SmartToolResult first = controller.ResolvePreview(session, firstRequest);
    SmartToolRequest changedRequest = firstRequest;
    changedRequest.PaletteColors[7U] = {0.08F, 0.76F, 0.31F, 1.0F};
    const SmartToolResult changed = controller.ResolvePreview(session, changedRequest);
    Require(first.HasPlan() && changed.HasPlan() && first.Plan.get() != changed.Plan.get() &&
            changed.Plan->Cells().front().AfterColor == changedRequest.PaletteColors[7U],
        "A changed palette snapshot reused a preview plan with obsolete colours.");
    SmartPreviewCache cache;
    static_cast<void>(cache.Resolve(first.Plan));
    const SmartPreviewData& data = cache.Resolve(changed.Plan);
    Require(cache.BuildCount() == 2U && data.GhostVoxels.front().Color[1] > 0.70F,
        "Preview cache did not rebuild for the changed materialized palette colour.");
}

void TestCubeSphereAndInvalid()
{
    SmartToolRequest cube = Request(SmartAction::Add, States{}, {3, 3, 3}, 3);
    cube.Geometry = SmartGeometry::Cube;
    const SmartPreviewData cubePreview = SmartPreviewEngine::Build(*Plan(cube));
    Require(cubePreview.GhostVoxels.size() == 27U && cubePreview.Bounds.Dimensions ==
            Asset::Voxel::VoxelDimensions{3U, 3U, 3U},
        "Cube preview did not expose every exact planned voxel and bounds.");

    SmartToolRequest sphere = Request(SmartAction::Add, States{}, {3, 3, 3}, 3);
    sphere.Geometry = SmartGeometry::Sphere;
    sphere.BrushRequest.State.Dimension = SmartBrushDimension::Surface2D;
    sphere.BrushRequest.State.Orientation = SmartBrushOrientation::Y;
    const SmartPreviewData spherePreview = SmartPreviewEngine::Build(*Plan(sphere));
    Require(!spherePreview.GhostVoxels.empty() &&
            spherePreview.GhostVoxels.size() < cubePreview.GhostVoxels.size() &&
            spherePreview.Brush.Dimension == SmartBrushDimension::Surface2D,
        "2D sphere preview did not retain its planned brush shape and dimension.");

    SmartToolRequest invalid = Request(SmartAction::Add, States{});
    invalid.BrushRequest.State.Size = 0;
    const SmartPreviewData invalidPreview = SmartPreviewEngine::Build(*Plan(invalid));
    Require(invalidPreview.Code == SmartBrushResultCode::InvalidRequest &&
            !invalidPreview.CanCommit() && invalidPreview.GhostVoxels.empty(),
        "Invalid planning was not represented without a synthetic ghost.");
}
}

void TestAggregatePlansSkipGhostConstruction()
{
    auto request = Request(SmartAction::Add, States{}, {16, 16, 16}, 8);
    request.BrushRequest.Dimensions = {32U, 32U, 32U};
    const SmartPreviewData preview = SmartPreviewEngine::Build(*Plan(request));
    Require(preview.RenderPlan.Mode == SmartBrushRenderMode::AggregateBox,
        "A 512-cell cube brush should ship an aggregate render plan.");
    Require(preview.GhostVoxels.empty() && preview.AffectedPositions.empty(),
        "Aggregate plans should not pay for per-cell ghost construction.");
    Require(preview.Statistics.Total > 256U && preview.CanCommit(),
        "Aggregate previews keep exact statistics and commitability.");
    Require(preview.Placement.IsActive() &&
            preview.Placement.Instances().empty() &&
            !preview.Placement.InstancesComplete() &&
            preview.Placement.RenderMode() ==
                VoxelPreviewRenderMode::AggregateBounds &&
            preview.Placement.Statistics().Total == preview.Statistics.Total,
        "Aggregate Smart Preview did not preserve the common exact statistics.");

    const SmartPreviewData detailed = SmartPreviewEngine::Build(
        *Plan(Request(SmartAction::Add, States{}, {2, 2, 2}, 2)));
    Require(detailed.RenderPlan.Mode == SmartBrushRenderMode::DetailedCells &&
            !detailed.GhostVoxels.empty(),
        "Small brushes must keep the exact per-cell ghost preview.");
}

int main()
{
    try
    {
        TestEmptyAndExactGhosts();
        TestDiagnosticsAndColours();
        TestCubeAndCache();
        TestPaletteSnapshotInvalidatesSessionPlan();
        TestCubeSphereAndInvalid();
        TestAggregatePlansSkipGhostConstruction();
        std::cout << "Smart Preview Engine tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
