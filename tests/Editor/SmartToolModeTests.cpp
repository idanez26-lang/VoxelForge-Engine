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
    const Asset::Voxel::VoxelDimensions dimensions = {128U, 128U, 128U},
    const Position normal = {0, 0, 0},
    const SmartBrushDimension dimension = SmartBrushDimension::Volume3D,
    const SmartBrushOrientation orientation = SmartBrushOrientation::Auto)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Pencil;
    request.Mode = mode;
    request.Action = action;
    request.BrushRequest.Dimensions = dimensions;
    // Shape is mode-owned. Dimension remains a Pencil brush setting, whereas
    // orientation is resolved to face/workplane Auto by the planner.
    request.BrushRequest.State.Shape = SmartBrushShape::Sphere;
    request.BrushRequest.State.Dimension = dimension;
    request.BrushRequest.State.Orientation = orientation;
    request.BrushRequest.State.Size = size;
    request.BrushRequest.State.PaletteIndex = 7U;
    request.BrushRequest.Placement = {target, normal};
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

SmartToolResult Resolve(const SmartToolRequest& request);

void TestPencilSurface2DModes()
{
    constexpr Asset::Voxel::VoxelDimensions dimensions{64U, 64U, 64U};
    const Position target{24, 24, 24};
    for (const Position normal : {Position{1, 0, 0}, Position{0, 1, 0},
             Position{0, 0, 1}})
    {
        for (const SmartToolMode mode : {SmartToolMode::SingleVoxel,
                 SmartToolMode::CubeBrush, SmartToolMode::SphereBrush,
                 SmartToolMode::CylinderBrush})
        {
            const SmartToolResult result = Resolve(Request(mode, SmartAction::Add,
                {}, 3, target, dimensions, normal, SmartBrushDimension::Surface2D,
                SmartBrushOrientation::Z));
            Require(result.HasPlan() &&
                    result.Plan->BrushState().Dimension ==
                        SmartBrushDimension::Surface2D &&
                    result.Plan->BrushState().Orientation == SmartBrushOrientation::Auto,
                "Pencil Surface2D did not preserve dimension or resolve Auto orientation.");
            if (mode == SmartToolMode::SingleVoxel)
                Require(result.Plan->Cells().size() == 1U,
                    "Pencil Surface2D Single is no longer one voxel.");
            for (const SmartToolPlanCell& cell : result.Plan->Cells())
            {
                if (normal.X != 0) Require(cell.WorldPosition.X == target.X,
                    "Pencil Surface2D did not project onto the X face.");
                if (normal.Y != 0) Require(cell.WorldPosition.Y == target.Y,
                    "Pencil Surface2D did not project onto the Y face.");
                if (normal.Z != 0) Require(cell.WorldPosition.Z == target.Z,
                    "Pencil Surface2D did not project onto the Z face.");
            }
        }
    }
}

SmartToolResult Resolve(const SmartToolRequest& request)
{
    SmartToolController controller;
    SmartToolSession session;
    return controller.ResolvePreview(session, request);
}

SmartBrushShape ShapeForMode(const SmartToolMode mode)
{
    switch (mode)
    {
    case SmartToolMode::SingleVoxel:
    case SmartToolMode::CubeBrush: return SmartBrushShape::Cube;
    case SmartToolMode::SphereBrush: return SmartBrushShape::Sphere;
    case SmartToolMode::CylinderBrush: return SmartBrushShape::Cylinder;
    }
    throw std::runtime_error("Unknown Smart Tool mode.");
}

void RequireCanonical(const SmartToolPlan& plan, const SmartToolMode expectedMode,
    const int expectedSize)
{
    Require(plan.Mode().has_value() && *plan.Mode() ==
            expectedMode &&
            plan.BrushState().Shape == ShapeForMode(expectedMode) &&
            plan.BrushState().Dimension == SmartBrushDimension::Volume3D &&
            plan.BrushState().Orientation == SmartBrushOrientation::Auto &&
            plan.BrushState().Size == expectedSize,
        "Planner did not canonicalize the exposed Smart Tool mode.");
}

void RequireSameCells(const SmartToolPlan& left, const SmartToolPlan& right)
{
    Require(left.Cells().size() == right.Cells().size(),
        "Repeated planning changed the number of Smart Brush cells.");
    for (std::size_t index = 0U; index < left.Cells().size(); ++index)
    {
        Require(left.Cells()[index].WorldPosition == right.Cells()[index].WorldPosition,
            "Repeated planning changed Smart Brush cell order or position.");
    }
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

void TestSphereAndCylinderShapes()
{
    const Position target{40, 40, 40};
    constexpr Asset::Voxel::VoxelDimensions dimensions{128U, 128U, 128U};
    for (const int size : {1, 3, 5, 7, 4, 64})
    {
        const auto result = Resolve(Request(SmartToolMode::SphereBrush,
            SmartAction::Add, {}, size, target, dimensions));
        const auto repeated = Resolve(Request(SmartToolMode::SphereBrush,
            SmartAction::Add, {}, size, target, dimensions));
        Require(result.HasPlan() && repeated.HasPlan(),
            "Sphere Brush did not produce a deterministic plan.");
        RequireCanonical(*result.Plan, SmartToolMode::SphereBrush, size);
        RequireSameCells(*result.Plan, *repeated.Plan);
        Require(SmartBrushEngine::EstimateTotal(result.Plan->BrushState()) ==
                result.Plan->Cells().size(),
            "Sphere Brush estimate differs from its exact planned volume.");
        const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
        const int radiusSquared = size * size;
        for (const SmartToolPlanCell& cell : result.Plan->Cells())
        {
            const int dx = 2 * (cell.WorldPosition.X - target.X) - evenCenterOffset;
            const int dy = 2 * (cell.WorldPosition.Y - target.Y) - evenCenterOffset;
            const int dz = 2 * (cell.WorldPosition.Z - target.Z) - evenCenterOffset;
            Require(dx * dx + dy * dy + dz * dz <= radiusSquared,
                "Sphere Brush generated a voxel outside its exact sphere.");
            Require(cell.After.Exists && cell.After.PaletteIndex == 7U,
                "Sphere Brush did not materialize the requested palette color.");
        }
    }

    for (const int size : {3, 5, 7, 4, 64})
    {
        const auto result = Resolve(Request(SmartToolMode::CylinderBrush,
            SmartAction::Add, {}, size, target, dimensions, {0, 1, 0}));
        const auto repeated = Resolve(Request(SmartToolMode::CylinderBrush,
            SmartAction::Add, {}, size, target, dimensions, {0, 1, 0}));
        Require(result.HasPlan() && repeated.HasPlan(),
            "Cylinder Brush did not produce a deterministic plan.");
        RequireCanonical(*result.Plan, SmartToolMode::CylinderBrush, size);
        RequireSameCells(*result.Plan, *repeated.Plan);
        Require(SmartBrushEngine::EstimateTotal(result.Plan->BrushState()) ==
                result.Plan->Cells().size(),
            "Cylinder Brush estimate differs from its exact planned volume.");
        const int evenCenterOffset = size % 2 == 0 ? 1 : 0;
        const int radiusSquared = size * size;
        for (const SmartToolPlanCell& cell : result.Plan->Cells())
        {
            const int dx = 2 * (cell.WorldPosition.X - target.X) - evenCenterOffset;
            const int dz = 2 * (cell.WorldPosition.Z - target.Z) - evenCenterOffset;
            Require(dx * dx + dz * dz <= radiusSquared &&
                    cell.WorldPosition.Y >= target.Y &&
                    cell.WorldPosition.Y < target.Y + size,
                "Cylinder Brush did not use its exact vertical voxel volume.");
        }
    }

    const auto sideCylinder = Resolve(Request(SmartToolMode::CylinderBrush,
        SmartAction::Add, {}, 3, target, dimensions, {1, 0, 0}));
    Require(sideCylinder.HasPlan() && sideCylinder.Plan->Bounds().Minimum.Y == target.Y - 1 &&
            sideCylinder.Plan->Bounds().Maximum.Y == target.Y + 1,
        "Cylinder Brush did not keep its vertical axis centered on a side surface.");
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

    const auto spherePaintMissing = Resolve(Request(SmartToolMode::SphereBrush,
        SmartAction::Paint, {}, 3));
    Require(spherePaintMissing.HasPlan() && !spherePaintMissing.Plan->HasChanges() &&
            std::all_of(spherePaintMissing.Plan->Cells().begin(),
                spherePaintMissing.Plan->Cells().end(),
                [](const SmartToolPlanCell& cell) { return !cell.After.Exists; }),
        "Sphere Paint created voxels that were absent from the document.");

    const States sphereOccupied{{{16, 16, 16}, {true, 3U}}};
    const auto sphereOverlap = Resolve(Request(SmartToolMode::SphereBrush,
        SmartAction::Add, sphereOccupied, 3));
    Require(sphereOverlap.HasPlan() &&
            sphereOverlap.Plan->PreviewDiagnostics().HasOverlap,
        "Sphere overlap was not retained in the immutable plan diagnostics.");

    const auto sphereRemove = Resolve(Request(SmartToolMode::SphereBrush,
        SmartAction::Erase, sphereOccupied, 3));
    Require(sphereRemove.HasPlan() &&
            sphereRemove.Plan->AffectedPositions().size() == 1U &&
            sphereRemove.Plan->AffectedPositions().front() == Position{16, 16, 16},
        "Sphere Remove did not affect exactly the existing source voxel.");

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

    const States cylinderOccupied{{{16, 16, 16}, {true, 3U}},
        {{17, 16, 16}, {true, 3U}}};
    const auto cylinderRemove = Resolve(Request(SmartToolMode::CylinderBrush,
        SmartAction::Erase, cylinderOccupied, 3));
    Require(cylinderRemove.HasPlan() &&
            cylinderRemove.Plan->AffectedPositions().size() == 2U &&
            std::all_of(cylinderRemove.Plan->AffectedPositions().begin(),
                cylinderRemove.Plan->AffectedPositions().end(),
                [&cylinderOccupied](const Position position)
                {
                    return cylinderOccupied.contains(position);
                }),
        "Cylinder Remove targeted cells that did not exist in the document.");

    const auto cylinderPaint = Resolve(Request(SmartToolMode::CylinderBrush,
        SmartAction::Paint, cylinderOccupied, 3));
    Require(cylinderPaint.HasPlan() &&
            cylinderPaint.Plan->AffectedPositions().size() == 2U &&
            std::all_of(cylinderPaint.Plan->Cells().begin(),
                cylinderPaint.Plan->Cells().end(),
                [](const SmartToolPlanCell& cell)
                {
                    return !cell.After.Exists || cell.After.PaletteIndex == 7U;
                }),
        "Cylinder Paint did not paint existing cells without creating new voxels.");

    const auto clipped = Resolve(Request(SmartToolMode::CubeBrush, SmartAction::Add,
        {}, 3, {3, 2, 2}, {4U, 4U, 4U}));
    Require(clipped.HasPlan() && clipped.Plan->Statistics().Clipped > 0U &&
            clipped.Plan->PreviewDiagnostics().HasOutOfBounds,
        "Cube Brush clipping did not remain visible in the plan diagnostics.");

    const auto cylinderClipped = Resolve(Request(SmartToolMode::CylinderBrush,
        SmartAction::Add, {}, 3, {3, 2, 2}, {4U, 4U, 4U}));
    Require(cylinderClipped.HasPlan() &&
            cylinderClipped.Plan->Statistics().Clipped > 0U &&
            cylinderClipped.Plan->PreviewDiagnostics().HasOutOfBounds,
        "Cylinder Brush clipping did not remain visible in the plan diagnostics.");

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult sizeThree = controller.ResolvePreview(session,
        Request(SmartToolMode::CubeBrush, SmartAction::Add, {}, 3));
    const SmartToolResult sizeFive = controller.ResolvePreview(session,
        Request(SmartToolMode::CubeBrush, SmartAction::Add, {}, 5));
    const SmartToolResult sphere = controller.ResolvePreview(session,
        Request(SmartToolMode::SphereBrush, SmartAction::Add, {}, 5));
    Require(sizeThree.HasPlan() && sizeFive.HasPlan() && sphere.HasPlan() &&
            sizeThree.Plan.get() != sizeFive.Plan.get() &&
            sizeFive.Plan.get() != sphere.Plan.get() &&
            sphere.Plan->BrushState().Shape == SmartBrushShape::Sphere,
        "Changing Smart Tool size or mode reused an obsolete immutable plan.");
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

    const auto unknownMode = Resolve(Request(
        static_cast<SmartToolMode>(255U), SmartAction::Add));
    Require(!unknownMode.HasPlan() &&
            unknownMode.Code == SmartBrushResultCode::Unsupported,
        "Planner accepted an unknown Smart Tool mode.");
}
}

int main()
{
    try
    {
        TestSingleActions();
        TestCubeSizesAndLimits();
        TestSphereAndCylinderShapes();
        TestDiagnosticsCacheAndPreview();
        TestEngineSafetyCap();
        TestPencilSurface2DModes();
        std::cout << "Smart Tool mode tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
