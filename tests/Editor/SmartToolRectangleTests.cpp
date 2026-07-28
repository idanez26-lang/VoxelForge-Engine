#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolFaceDepthDrag.h"
#include "SmartTools/SmartToolStroke.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;
struct Hash { std::size_t operator()(const Position p) const noexcept
{ return static_cast<std::size_t>(static_cast<std::uint32_t>(p.X)) ^
    (static_cast<std::size_t>(p.Y) << 11U) ^ (static_cast<std::size_t>(p.Z) << 22U); } };
using States = std::unordered_map<Position, SmartToolVoxelState, Hash>;
void Require(const bool value, const std::string_view message)
{ if (!value) throw std::runtime_error(std::string(message)); }

SmartToolPlanPtr Plan(const SmartAction action, const States& states,
    const Position a, const Position b, const Position normal = {0, 1, 0},
    const SmartToolMode mode = SmartToolMode::SingleVoxel, const int size = 1,
    const int height = 1)
{
    const float surface = normal.X != 0 ? static_cast<float>(a.X) :
        normal.Y != 0 ? static_cast<float>(a.Y) : static_cast<float>(a.Z);
    const auto plane = SmartToolPlanner::MakeGeometryPlane(a, normal, surface);
    Require(plane.has_value(), "Geometry plane was not canonicalized.");
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Geometry;
    request.GeometryHeight = height;
    request.Mode = mode;
    request.Action = action;
    request.GeometryPlane = *plane;
    request.BrushRequest = {{16U, 16U, 16U}, {SmartBrushShape::Cube,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Auto, size, 9U,
        SmartBrushMode::Add}, {SmartToolPlanner::ProjectGeometryEndpoint(*plane, b),
        normal}, {}};
    request.ReadVoxel = [&states](const Position p) { const auto found = states.find(p);
        return found == states.end() ? SmartToolVoxelState{} : found->second; };
    request.SourceIdentity = 0x5EC7U;
    request.SourceRevision = 1U;
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Geometry did not produce an immutable plan.");
    Require(controller.ResolveCommit(session).Plan == result.Plan,
        "Geometry preview and commit did not retain the same plan.");
    return result.Plan;
}

void TestPlanesAndActions()
{
    const States empty;
    const auto positiveFace = SmartToolPlanner::MakeGeometryPlane(
        {8, 3, 2}, {1, 0, 0}, 8.0F);
    const auto negativeFace = SmartToolPlanner::MakeGeometryPlane(
        {7, 3, 2}, {-1, 0, 0}, 8.0F);
    Require(positiveFace && negativeFace &&
        SmartToolPlanner::ProjectGeometryEndpoint(*positiveFace, {99, 4, 5}).X == 8 &&
        SmartToolPlanner::ProjectGeometryEndpoint(*negativeFace, {99, 4, 5}).X == 8,
        "Rectangle positive/negative faces did not preserve their geometric surface.");
    Require(Plan(SmartAction::Add, empty, {2, 3, 4}, {5, 99, 6})->Cells().size() == 12U,
        "Rectangle Workplane XZ did not lock its Y plane.");
    Require(Plan(SmartAction::Add, empty, {2, 3, 4}, {99, 5, 6}, {1, 0, 0})->Cells().size() == 9U,
        "Rectangle Face YZ did not lock its X plane.");
    Require(Plan(SmartAction::Add, empty, {2, 3, 4}, {5, 6, 99}, {0, 0, 1})->Cells().size() == 16U,
        "Rectangle Face XY did not lock its Z plane.");
    States occupied;
    for (int x = 2; x <= 3; ++x) for (int z = 4; z <= 5; ++z)
        occupied.emplace(Position{x, 3, z}, SmartToolVoxelState{true, 2U});
    Require(Plan(SmartAction::Paint, occupied, {2, 3, 4}, {3, 3, 5})->HasChanges(),
        "Rectangle Paint did not use the shared action path.");
    Require(Plan(SmartAction::Erase, occupied, {2, 3, 4}, {3, 3, 5})->HasChanges(),
        "Rectangle Remove did not use the shared action path.");
    Require(!Plan(SmartAction::Add, occupied, {2, 3, 4}, {3, 3, 5})->HasChanges(),
        "Rectangle Add did not preserve occupied no-change semantics.");
    const SmartToolPlanPtr clipped = Plan(SmartAction::Add, empty,
        {14, 3, 14}, {18, 3, 18});
    Require(clipped->Statistics().Clipped > 0U && clipped->HasChanges() &&
            clipped->PreviewDiagnostics().HasOutOfBounds,
        "Rectangle OutOfBounds did not expose clipping diagnostics without mutation.");
}

void TestCircleAndCylinder()
{
    const States empty;
    const SmartToolPlanPtr circle = Plan(SmartAction::Add, empty,
        {4, 4, 4}, {6, 4, 4}, {0, 1, 0},
        SmartToolMode::SphereBrush, 9);
    Require(circle->Geometry() == SmartGeometry::Geometry &&
            circle->Mode() == SmartToolMode::SphereBrush &&
            circle->BrushState().Size == 9 &&
            circle->Cells().size() == 13U &&
            circle->Bounds().Dimensions.Y == 1U,
        "Circle did not rasterize the deterministic filled Euclidean disk.");
    bool hasBoundingBoxCorner = false;
    for (const SmartToolPlanCell& cell : circle->Cells())
        hasBoundingBoxCorner |=
            std::abs(cell.WorldPosition.X - 4) == 2 &&
            std::abs(cell.WorldPosition.Z - 4) == 2;
    Require(!hasBoundingBoxCorner,
        "Sphere mode included a bounding-box corner outside the disk.");
    const SmartToolPlanPtr center = Plan(SmartAction::Add, empty,
        {4, 4, 4}, {4, 4, 4}, {0, 1, 0},
        SmartToolMode::SphereBrush, 9);
    Require(center->Cells().size() == 1U,
        "Circle minimum radius did not retain its center voxel.");

    const SmartToolPlanPtr cylinder = Plan(SmartAction::Add, empty,
        {4, 4, 4}, {6, 4, 4}, {0, 1, 0},
        SmartToolMode::CylinderBrush, 9, 3);
    Require(cylinder->Mode() == SmartToolMode::CylinderBrush &&
            cylinder->GeometryHeight() == 3 &&
            cylinder->BrushState().Size == 9 &&
            cylinder->Cells().size() == 39U &&
            cylinder->Bounds().Dimensions.Y == 3U,
        "Cylinder did not extrude the exact disk by three layers.");
    const SmartToolPlanPtr negative = Plan(SmartAction::Add, empty,
        {4, 4, 4}, {6, 4, 4}, {0, 1, 0},
        SmartToolMode::CylinderBrush, 7, -3);
    bool hasNegativeLayer = false;
    for (const SmartToolPlanCell& cell : negative->Cells())
        hasNegativeLayer |= cell.WorldPosition.Y == 2;
    Require(hasNegativeLayer && negative->Cells().size() == 39U,
        "Negative Cylinder height did not extrude opposite the locked normal.");
    Require(circle->CacheKey() != cylinder->CacheKey() &&
            cylinder->CacheKey() != negative->CacheKey(),
        "Geometry mode or signed height was omitted from cache identity.");

    const SmartToolFaceDepthDragAxis horizontal{{1.0F, 0.0F}, false};
    Require(ResolveSmartToolGeometryHeight(horizontal, {0.0F, 0.0F}) == 1 &&
            ResolveSmartToolGeometryHeight(horizontal, {25.0F, 0.0F}) == 2 &&
            ResolveSmartToolGeometryHeight(horizontal, {-25.0F, 0.0F}) == -2 &&
            ResolveSmartToolGeometryHeight(horizontal, {100000.0F, 0.0F},
                MaximumSmartGeometryHeight) == MaximumSmartGeometryHeight,
        "Cylinder height phase did not preserve signed integral layers.");
}

void TestModesAndReplacement()
{
    const States empty;
    Require(Plan(SmartAction::Add, empty, {4, 4, 4}, {6, 4, 6},
            {0, 1, 0}, SmartToolMode::SingleVoxel, 1)->HasChanges(),
        "Geometry Single mode did not retain Rectangle.");
    Require(Plan(SmartAction::Add, empty, {4, 4, 4}, {6, 4, 6},
            {0, 1, 0}, SmartToolMode::CubeBrush, 3)->HasChanges(),
        "Geometry Cube mode did not retain the historical Rectangle brush.");
    Require(Plan(SmartAction::Add, empty, {4, 4, 4}, {6, 4, 6},
            {0, 1, 0}, SmartToolMode::SphereBrush, 3)->HasChanges(),
        "Geometry Sphere mode did not produce a disk.");
    Require(Plan(SmartAction::Add, empty, {4, 4, 4}, {6, 4, 6},
            {0, 1, 0}, SmartToolMode::CylinderBrush, 3, 2)->HasChanges(),
        "Geometry Cylinder mode did not produce an extrusion.");
    const auto first = Plan(SmartAction::Add, empty, {2, 2, 2}, {7, 2, 7});
    const auto last = Plan(SmartAction::Add, empty, {2, 2, 2}, {3, 2, 3});
    SmartToolStroke stroke;
    Require(stroke.Begin({0x5EC7U, 1U, 0U, 0U, [](Position) { return SmartToolVoxelState{}; }},
        SmartAction::Add, {2, 2, 2}, {0, 1, 0}) &&
        stroke.ReplaceWithPlan(*first) && stroke.ReplaceWithPlan(*last) &&
        stroke.Changes().size() == last->Cells().size(),
        "Rectangle retained a stale prior plan during drag replacement.");
}
}

int main()
{
    try { TestPlanesAndActions(); TestCircleAndCylinder();
        TestModesAndReplacement();
        std::cout << "Smart Tool Geometry tests passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
