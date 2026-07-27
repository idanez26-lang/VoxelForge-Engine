#include "SmartTools/SmartPreviewEngine.h"
#include "SmartTools/SmartToolController.h"

#include <algorithm>
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

SmartToolRequest Request(const SmartToolMode mode, const SmartAction action,
    const States& states = {}, const int size = 1,
    const Position target = {16, 16, 16},
    const Asset::Voxel::VoxelDimensions dimensions = {128U, 128U, 128U})
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = dimensions;
    // Deliberately hostile values demonstrate planner-boundary normalization.
    request.BrushRequest.State.Shape = SmartBrushShape::Sphere;
    request.BrushRequest.State.Dimension = SmartBrushDimension::Surface2D;
    request.BrushRequest.State.Orientation = SmartBrushOrientation::Z;
    request.BrushRequest.State.Size = size;
    request.BrushRequest.State.PaletteIndex = 7U;
    request.BrushRequest.Placement = {target, {0, 0, 0}};
    const auto snapshot = std::make_shared<States>(states);
    request.ReadVoxel = [snapshot](const Position position)
    {
        const auto found = snapshot->find(position);
        return found == snapshot->end() ? SmartToolVoxelState{} : found->second;
    };
    request.SourceIdentity = 0x404U;
    request.SourceRevision = 4U;
    request.SourceGeneration = 1U;
    request.HasPaletteColors = true;
    request.PaletteColors[3U] = {0.12F, 0.33F, 0.72F, 1.0F};
    request.PaletteColors[7U] = {0.86F, 0.24F, 0.38F, 1.0F};
    return request;
}

SmartToolResult Resolve(const SmartToolRequest& request)
{
    SmartToolController controller;
    SmartToolSession session;
    return controller.ResolvePreview(session, request);
}

void RequireCanonical(const SmartToolPlan& plan, const SmartToolMode expectedMode,
    const int expectedSize)
{
    Require(plan.Mode().has_value() && *plan.Mode() ==
            expectedMode &&
            plan.BrushState().Shape == SmartBrushShape::Cube &&
            plan.BrushState().Dimension == SmartBrushDimension::Volume3D &&
            plan.BrushState().Orientation == SmartBrushOrientation::Auto &&
            plan.BrushState().Size == expectedSize,
        "Planner did not canonicalize the exposed Smart Tool mode.");
}

void TestSingleActions()
{
    const auto add = Resolve(Request(SmartToolMode::SingleVoxel, SmartAction::Add,
        {}, 9));
    Require(add.HasPlan() && add.Plan->Cells().size() == 1U &&
            add.Plan->Cells().front().Operation == SmartToolCellOperation::Add,
        "Single Voxel Create did not create exactly one add cell.");
    RequireCanonical(*add.Plan, SmartToolMode::SingleVoxel, 1);

    const States existing{{{16, 16, 16}, {true, 3U}}};
    const auto erase = Resolve(Request(SmartToolMode::SingleVoxel, SmartAction::Erase,
        existing, 5));
    Require(erase.HasPlan() && erase.Plan->Cells().size() == 1U &&
            erase.Plan->Cells().front().Operation == SmartToolCellOperation::Erase,
        "Single Voxel Remove did not resolve one exact erase cell.");
    RequireCanonical(*erase.Plan, SmartToolMode::SingleVoxel, 1);

    const auto paint = Resolve(Request(SmartToolMode::SingleVoxel, SmartAction::Paint,
        existing, 3));
    Require(paint.HasPlan() && paint.Plan->Cells().size() == 1U &&
            paint.Plan->Cells().front().Operation == SmartToolCellOperation::Paint &&
            paint.Plan->Cells().front().After.PaletteIndex == 7U,
        "Single Voxel Paint did not change only the existing target cell.");
}

void TestCubeSizesAndLimits()
{
    for (const int size : {1, 3, 5, 9})
    {
        const auto result = Resolve(Request(SmartToolMode::CubeBrush,
            SmartAction::Add, {}, size));
        Require(result.HasPlan() && result.Plan->Cells().size() ==
                static_cast<std::size_t>(size * size * size),
            "Cube Brush count differs from its exact voxel plan.");
        RequireCanonical(*result.Plan, SmartToolMode::CubeBrush, size);
    }
    const auto maximum = Resolve(Request(SmartToolMode::CubeBrush,
        SmartAction::Add, {}, 64, {32, 32, 32}));
    Require(maximum.HasPlan() && maximum.Plan->Cells().size() == 262144U &&
            maximum.Plan->Statistics().Changed == 262144U,
        "Cube Brush did not accept the dedicated size-64 limit.");
    Require(!Resolve(Request(SmartToolMode::CubeBrush, SmartAction::Add, {}, 0)).HasPlan() &&
            !Resolve(Request(SmartToolMode::CubeBrush, SmartAction::Add, {}, 65)).HasPlan(),
        "Cube Brush accepted an invalid size outside [1, 64].");
}

void TestDiagnosticsCacheAndPreview()
{
    const States occupied{{{16, 16, 16}, {true, 3U}}};
    const auto overlap = Resolve(Request(SmartToolMode::SingleVoxel, SmartAction::Add,
        occupied));
    Require(overlap.HasPlan() && overlap.Plan->PreviewDiagnostics().HasOverlap &&
            overlap.Plan->PreviewDiagnostics().HasNoChange && !overlap.Plan->HasChanges(),
        "Single Voxel overlap was not retained in the immutable plan diagnostics.");

    const auto noChange = Resolve(Request(SmartToolMode::SingleVoxel, SmartAction::Paint,
        {{{16, 16, 16}, {true, 7U}}}));
    Require(noChange.HasPlan() && noChange.Plan->PreviewDiagnostics().HasNoChange &&
            !noChange.Plan->HasChanges(),
        "Painting an existing matching color did not remain a no-change plan.");

    const auto paintMissing = Resolve(Request(SmartToolMode::SingleVoxel,
        SmartAction::Paint));
    Require(paintMissing.HasPlan(),
        "Paint on a missing voxel did not produce a diagnostic plan.");
    const SmartPreviewData missingPreview = SmartPreviewEngine::Build(*paintMissing.Plan);
    Require(paintMissing.Plan->Cells().size() == 1U &&
            !paintMissing.Plan->Cells().front().Before.Exists &&
            !paintMissing.Plan->Cells().front().After.Exists &&
            !paintMissing.Plan->HasChanges() &&
            missingPreview.AffectedPositions.empty() && !missingPreview.CanCommit(),
        "Paint changed or previewed a missing voxel as a committable edit.");

    const States sparse{{{15, 15, 15}, {true, 3U}},
        {{16, 16, 16}, {true, 3U}}};
    const auto mixedRemove = Resolve(Request(SmartToolMode::CubeBrush,
        SmartAction::Erase, sparse, 3));
    Require(mixedRemove.HasPlan(),
        "Cube Remove did not produce a diagnostic plan.");
    const SmartPreviewData removePreview = SmartPreviewEngine::Build(*mixedRemove.Plan);
    Require(removePreview.AffectedPositions.size() == 2U &&
            std::find(removePreview.AffectedPositions.begin(),
                removePreview.AffectedPositions.end(), Position{15, 15, 15}) !=
                removePreview.AffectedPositions.end() &&
            std::find(removePreview.AffectedPositions.begin(),
                removePreview.AffectedPositions.end(), Position{16, 16, 16}) !=
                removePreview.AffectedPositions.end(),
        "Cube Remove preview did not retain exactly the existing cells to erase.");

    const auto clipped = Resolve(Request(SmartToolMode::CubeBrush, SmartAction::Add,
        {}, 3, {3, 2, 2}, {4U, 4U, 4U}));
    Require(clipped.HasPlan() && clipped.Plan->Statistics().Clipped > 0U &&
            clipped.Plan->PreviewDiagnostics().HasOutOfBounds,
        "Cube Brush clipping did not remain visible in the plan diagnostics.");

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult sizeThree = controller.ResolvePreview(session,
        Request(SmartToolMode::CubeBrush, SmartAction::Add, {}, 3));
    const SmartToolResult sizeFive = controller.ResolvePreview(session,
        Request(SmartToolMode::CubeBrush, SmartAction::Add, {}, 5));
    Require(sizeThree.HasPlan() && sizeFive.HasPlan() &&
            sizeThree.Plan.get() != sizeFive.Plan.get(),
        "Changing Cube Brush size reused an obsolete immutable plan.");
    SmartPreviewCache cache;
    const SmartPreviewData& preview = cache.Resolve(sizeFive.Plan);
    Require(preview.GhostVoxels.size() == sizeFive.Plan->Cells().size() &&
            preview.AffectedPositions == sizeFive.Plan->AffectedPositions() &&
            preview.CanCommit(),
        "Smart Preview no longer displays the exact Cube plan that commit receives.");
}

void TestEngineSafetyCap()
{
    SmartBrushRequest request;
    request.Dimensions = {128U, 128U, 128U};
    request.State.Shape = SmartBrushShape::Cube;
    request.State.Size = 1;
    request.State.PaletteIndex = 1U;
    request.IsOccupied = [](const Position) { return false; };
    request.MaximumSize = MaximumSmartBrushRequestSize + 1;
    const SmartBrushResult result = SmartBrushEngine::Resolve(request);
    Require(result.Code == SmartBrushResultCode::InvalidRequest,
        "Smart Brush accepted an unsafe caller-provided maximum size.");
}
}

int main()
{
    try
    {
        TestSingleActions();
        TestCubeSizesAndLimits();
        TestDiagnosticsCacheAndPreview();
        TestEngineSafetyCap();
        std::cout << "Smart Tool mode tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
