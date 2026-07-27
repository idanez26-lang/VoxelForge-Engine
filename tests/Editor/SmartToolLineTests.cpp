#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolStroke.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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
            (static_cast<std::size_t>(position.Y) << 11U) ^
            (static_cast<std::size_t>(position.Z) << 22U);
    }
};
using States = std::unordered_map<Position, SmartToolVoxelState, PositionHash>;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

SmartToolRequest Request(const SmartAction action, const States& states,
    const Position pointA, const Position pointB, const int size = 1,
    const SmartToolMode mode = SmartToolMode::SingleVoxel)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Line;
    request.Mode = mode;
    request.Action = action;
    request.LineStart = pointA;
    request.BrushRequest.Dimensions = {16U, 16U, 16U};
    request.BrushRequest.State = {SmartBrushShape::Cube,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Auto,
        size, 9U, SmartBrushMode::Add};
    request.BrushRequest.Placement = {pointB, {0, 1, 0}};
    request.ReadVoxel = [&states](const Position position)
    {
        const auto found = states.find(position);
        return found == states.end() ? SmartToolVoxelState{} : found->second;
    };
    request.SourceIdentity = 0x1A1EU;
    request.SourceRevision = 1U;
    return request;
}

SmartToolPlanPtr Plan(const SmartToolRequest& request)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Line request did not create a plan.");
    Require(controller.ResolveCommit(session).Plan == result.Plan,
        "Line commit did not preserve the rendered immutable plan.");
    return result.Plan;
}

void TestAxesAndDiagonal()
{
    const States empty;
    const auto check = [&empty](const Position a, const Position b, const std::size_t count)
    {
        const SmartToolPlanPtr plan = Plan(Request(SmartAction::Add, empty, a, b));
        Require(plan->Cells().size() == count && plan->HasChanges() &&
                plan->Cells().front().WorldPosition == a &&
                plan->Cells().back().WorldPosition == b,
            "Line interpolation was not inclusive, continuous, and deterministic.");
    };
    check({1, 2, 3}, {6, 2, 3}, 6U);
    check({1, 2, 3}, {1, 7, 3}, 6U);
    check({1, 2, 3}, {1, 2, 8}, 6U);
    check({1, 1, 1}, {6, 4, 3}, 6U);

    const SmartToolPlanPtr diagonal = Plan(Request(SmartAction::Add, empty,
        {1, 1, 1}, {6, 4, 3}));
    const auto& cells = diagonal->Cells();
    for (std::size_t index = 1U; index < cells.size(); ++index)
    {
        const Position previous = cells[index - 1U].WorldPosition;
        const Position current = cells[index].WorldPosition;
        Require(std::abs(current.X - previous.X) <= 1 &&
                std::abs(current.Y - previous.Y) <= 1 &&
                std::abs(current.Z - previous.Z) <= 1,
            "Line rasterization contains a gap between adjacent samples.");
    }
}

void TestReplaceAndFrameIndependence()
{
    const States empty;
    const Position pointA{2, 2, 2};
    const SmartToolPlanPtr direct = Plan(Request(SmartAction::Add, empty,
        pointA, {8, 5, 4}));
    const SmartToolPlanPtr intermediate = Plan(Request(SmartAction::Add, empty,
        pointA, {5, 3, 3}));

    SmartToolStroke stroke;
    Require(stroke.Begin({0x1A1EU, 1U, 0U, 0U,
            [&empty](const Position position)
            {
                const auto found = empty.find(position);
                return found == empty.end() ? SmartToolVoxelState{} : found->second;
            }}, SmartAction::Add, pointA, {0, 1, 0}),
        "Line frame-independence stroke could not start.");
    Require(stroke.ReplaceWithPlan(*intermediate),
        "Line intermediate endpoint did not replace its pending plan.");
    Require(stroke.ReplaceWithPlan(*direct),
        "Line final endpoint did not replace its pending plan.");

    const auto changes = stroke.Changes();
    Require(changes.size() == direct->Cells().size(),
        "Line final endpoint retained stale cells from an earlier frame.");
    for (std::size_t index = 0U; index < changes.size(); ++index)
        Require(changes[index].Position == direct->Cells()[index].WorldPosition,
            "Line direct and intermediate-frame planning diverged at the final endpoint.");

    const SmartToolPlanPtr extended = Plan(Request(SmartAction::Add, empty,
        pointA, {9, 2, 2}));
    const SmartToolPlanPtr shortened = Plan(Request(SmartAction::Add, empty,
        pointA, {4, 2, 2}));
    Require(stroke.ReplaceWithPlan(*extended) && stroke.ReplaceWithPlan(*shortened),
        "Line endpoint shortening could not replace the prior plan.");
    const auto shortenedChanges = stroke.Changes();
    Require(shortenedChanges.size() == 3U,
        "Line endpoint shortening retained voxels beyond the final endpoint.");
    for (const auto& change : shortenedChanges)
        Require(change.Position.X <= 4,
            "Line endpoint shortening retained an extended voxel.");
}

void TestReversibleTieDiagonals()
{
    const States empty;
    // This 2:1 diagonal has a Bresenham tie at its middle sample. Its voxel
    // set must be independent of whether the artist dragged A->B or B->A.
    const Position pointA{0, 0, 0};
    const Position pointB{2, 1, 0};
    const SmartToolPlanPtr forward = Plan(Request(SmartAction::Add, empty,
        pointA, pointB));
    const SmartToolPlanPtr backward = Plan(Request(SmartAction::Add, empty,
        pointB, pointA));
    Require(forward->Cells().size() == backward->Cells().size(),
        "Line reverse drag changed the number of tie-diagonal voxels.");
    for (std::size_t index = 0U; index < forward->Cells().size(); ++index)
        Require(forward->Cells()[index].WorldPosition ==
                backward->Cells()[index].WorldPosition,
            "Line reverse drag chose a different tie-diagonal voxel.");
}

void TestActionsAndBrushModes()
{
    States existing;
    for (int x = 2; x <= 5; ++x) existing.emplace(Position{x, 3, 3},
        SmartToolVoxelState{true, 2U});
    const SmartToolPlanPtr paint = Plan(Request(SmartAction::Paint, existing,
        {2, 3, 3}, {5, 3, 3}));
    const SmartToolPlanPtr erase = Plan(Request(SmartAction::Erase, existing,
        {2, 3, 3}, {5, 3, 3}));
    Require(paint->Cells().size() == 4U && paint->HasChanges() &&
            erase->Cells().size() == 4U && erase->HasChanges(),
        "Line Paint/Remove did not resolve through the shared action path.");
    const States empty;
    const SmartToolPlanPtr cube = Plan(Request(SmartAction::Add, empty,
        {5, 5, 5}, {6, 5, 5}, 2, SmartToolMode::CubeBrush));
    const SmartToolPlanPtr sphere = Plan(Request(SmartAction::Add, empty,
        {5, 5, 5}, {6, 5, 5}, 3, SmartToolMode::SphereBrush));
    const SmartToolPlanPtr cylinder = Plan(Request(SmartAction::Add, empty,
        {5, 5, 5}, {6, 5, 5}, 3, SmartToolMode::CylinderBrush));
    Require(cube->Cells().size() > 2U && sphere->Cells().size() > 2U &&
            cylinder->Cells().size() > 2U,
        "Line did not apply the existing cube, sphere, and cylinder brushes per sample.");
}

void TestNoChangeAndClipping()
{
    States occupied;
    occupied.emplace(Position{1, 1, 1}, SmartToolVoxelState{true, 9U});
    const SmartToolPlanPtr noChange = Plan(Request(SmartAction::Add, occupied,
        {1, 1, 1}, {1, 1, 1}));
    Require(!noChange->HasChanges() && noChange->Statistics().Unchanged == 1U,
        "Line Add did not report an occupied single-voxel line as no-change.");
    const States empty;
    const SmartToolPlanPtr clipped = Plan(Request(SmartAction::Add, empty,
        {14, 1, 1}, {18, 1, 1}));
    Require(clipped->Statistics().Clipped > 0U && clipped->HasChanges(),
        "Line clipping did not retain the valid in-bounds portion.");
}
}

int main()
{
    try
    {
        TestAxesAndDiagonal();
        TestActionsAndBrushModes();
        TestNoChangeAndClipping();
        TestReplaceAndFrameIndependence();
        TestReversibleTieDiagonals();
        std::cout << "Smart Tool Line tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
