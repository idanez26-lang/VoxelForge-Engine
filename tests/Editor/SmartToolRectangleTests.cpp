#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolStroke.h"

#include <cstdint>
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
    const SmartToolMode mode = SmartToolMode::SingleVoxel, const int size = 1)
{
    const float surface = normal.X != 0 ? static_cast<float>(a.X) :
        normal.Y != 0 ? static_cast<float>(a.Y) : static_cast<float>(a.Z);
    const auto plane = SmartToolPlanner::MakeRectanglePlane(a, normal, surface);
    Require(plane.has_value(), "Rectangle plane was not canonicalized.");
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Rectangle;
    request.Mode = mode;
    request.Action = action;
    request.RectanglePlane = *plane;
    request.BrushRequest = {{16U, 16U, 16U}, {SmartBrushShape::Cube,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Auto, size, 9U,
        SmartBrushMode::Add}, {SmartToolPlanner::ProjectRectangleEndpoint(*plane, b),
        normal}, {}};
    request.ReadVoxel = [&states](const Position p) { const auto found = states.find(p);
        return found == states.end() ? SmartToolVoxelState{} : found->second; };
    request.SourceIdentity = 0x5EC7U;
    request.SourceRevision = 1U;
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Rectangle did not produce an immutable plan.");
    Require(controller.ResolveCommit(session).Plan == result.Plan,
        "Rectangle preview and commit did not retain the same plan.");
    return result.Plan;
}

void TestPlanesAndActions()
{
    const States empty;
    const auto positiveFace = SmartToolPlanner::MakeRectanglePlane(
        {8, 3, 2}, {1, 0, 0}, 8.0F);
    const auto negativeFace = SmartToolPlanner::MakeRectanglePlane(
        {7, 3, 2}, {-1, 0, 0}, 8.0F);
    Require(positiveFace && negativeFace &&
        SmartToolPlanner::ProjectRectangleEndpoint(*positiveFace, {99, 4, 5}).X == 8 &&
        SmartToolPlanner::ProjectRectangleEndpoint(*negativeFace, {99, 4, 5}).X == 8,
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

void TestModesAndReplacement()
{
    const States empty;
    for (const SmartToolMode mode : {SmartToolMode::SingleVoxel,
            SmartToolMode::CubeBrush, SmartToolMode::SphereBrush,
            SmartToolMode::CylinderBrush})
        Require(Plan(SmartAction::Add, empty, {4, 4, 4}, {6, 4, 6}, {0, 1, 0},
            mode, mode == SmartToolMode::SingleVoxel ? 1 : 3)->HasChanges(),
            "Rectangle brush mode did not use SmartBrushEngine.");
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
    try { TestPlanesAndActions(); TestModesAndReplacement();
        std::cout << "Smart Tool Rectangle tests passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
