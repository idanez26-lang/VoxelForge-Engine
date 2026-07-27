#include "SmartTools/SmartToolController.h"
#include "SmartTools/SmartToolFaceDepthDrag.h"

#include <array>
#include <cstdint>
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

Position Add(const Position position, const Position delta)
{
    return {position.X + delta.X, position.Y + delta.Y, position.Z + delta.Z};
}

SmartToolRequest Request(const SmartAction action, const States& states,
    const Position seed, const Position normal, const int size = 1,
    const std::uint8_t palette = 9U, const int depth = 1)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Face;
    request.Action = action;
    request.BrushRequest.Dimensions = {7U, 7U, 7U};
    // Face intentionally ignores these legacy Brush shape settings.
    request.BrushRequest.State = {SmartBrushShape::Sphere,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Z,
        size, palette, SmartBrushMode::Add};
    request.BrushRequest.Placement = {
        action == SmartAction::Add ? Add(seed, normal) : seed, normal};
    request.FaceSeed = {seed, normal};
    request.FaceDepth = depth;
    request.ReadVoxel = [&states](const Position position)
    {
        const auto found = states.find(position);
        return found == states.end() ? SmartToolVoxelState{} : found->second;
    };
    request.SourceIdentity = 0xFACEU;
    request.SourceRevision = 7U;
    return request;
}

SmartToolPlanPtr Plan(const SmartToolRequest& request)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session, request);
    Require(result.HasPlan(), "Face planning did not produce an immutable plan.");
    return result.Plan;
}

void TestSixNormalsAndOneLayer()
{
    const std::array<Position, 6U> normals{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const Position seed{3, 3, 3};
    for (const Position normal : normals)
    {
        const States states{{seed, {true, 2U}}};
        const SmartToolPlanPtr plan = Plan(Request(SmartAction::Add, states, seed, normal));
        Require(plan->Cells().size() == 1U &&
                plan->Cells().front().WorldPosition == Add(seed, normal) &&
                plan->Cells().front().Operation == SmartToolCellOperation::Add &&
                plan->Bounds().Dimensions.X * plan->Bounds().Dimensions.Y *
                    plan->Bounds().Dimensions.Z == 1U,
            "Face Add did not produce one layer for every axis normal.");
    }
}

States Plane(const Position seed, const Position normal, const int size)
{
    States states;
    const int minimum = -((size - 1) / 2);
    const int maximum = size / 2;
    for (int second = minimum; second <= maximum; ++second)
        for (int first = minimum; first <= maximum; ++first)
        {
            Position position = seed;
            if (normal.X != 0) { position.Y += first; position.Z += second; }
            else if (normal.Y != 0) { position.X += first; position.Z += second; }
            else { position.X += first; position.Y += second; }
            states.emplace(position, SmartToolVoxelState{true, 2U});
        }
    return states;
}

bool HasCell(const SmartToolPlan& plan, const Position position)
{
    for (const SmartToolPlanCell& cell : plan.Cells())
        if (cell.WorldPosition == position) return true;
    return false;
}

void TestSquareMaskFiltersHolesAndHiddenCells()
{
    const Position seed{3, 3, 3};
    const Position normal{0, 1, 0};
    States states = Plane(seed, normal, 3);
    const Position hole{2, 3, 3};
    const Position hidden{4, 3, 3};
    const Position disconnected{0, 3, 0};
    states.erase(hole);
    states.emplace(Add(hidden, normal), SmartToolVoxelState{true, 6U});
    states.emplace(disconnected, SmartToolVoxelState{true, 2U});
    const SmartToolPlanPtr add = Plan(Request(SmartAction::Add, states, seed, normal, 3));
    Require(add->Cells().size() == 7U && !HasCell(*add, Add(hole, normal)) &&
            !HasCell(*add, Add(hidden, normal)) &&
            !HasCell(*add, Add(disconnected, normal)),
        "Face Add did not preserve the connected irregular surface mask.");
    for (const SmartToolPlanCell& cell : add->Cells())
        Require(cell.WorldPosition.Y == 4 && cell.Operation == SmartToolCellOperation::Add,
            "Face Add escaped its exact visible layer.");

    const SmartToolPlanPtr erase = Plan(Request(SmartAction::Erase, states, seed, normal, 3));
    Require(erase->Cells().size() == 7U && !HasCell(*erase, hole) &&
            !HasCell(*erase, hidden),
        "Face Remove did not affect only exposed support cells.");
    for (const SmartToolPlanCell& cell : erase->Cells())
        Require(cell.WorldPosition.Y == 3 && cell.Operation == SmartToolCellOperation::Erase,
            "Face Remove escaped the touched visible layer.");

    const SmartToolPlanPtr paint = Plan(Request(SmartAction::Paint, states, seed, normal, 3));
    Require(paint->Cells().size() == 7U,
        "Face Paint did not preserve the same visible face mask.");
    for (const SmartToolPlanCell& cell : paint->Cells())
        Require(cell.After == SmartToolVoxelState{true, 9U} &&
                cell.Operation == SmartToolCellOperation::Paint,
            "Face Paint did not use the common action resolver.");
}

void TestSizeAndShapeAreIgnoredForCompleteSurface()
{
    const Position seed{3, 3, 3};
    const Position normal{0, 0, 1};
    States states = Plane(seed, normal, 5);
    const SmartToolPlanPtr odd = Plan(Request(SmartAction::Paint, states, seed, normal, 3));
    const SmartToolPlanPtr even = Plan(Request(SmartAction::Paint, states, seed, normal, 2));
    Require(odd->Cells().size() == states.size() &&
            even->Cells().size() == states.size(),
        "Face did not resolve the entire connected surface independently of size.");
    for (const SmartToolPlanCell& cell : even->Cells())
        Require(cell.WorldPosition.Z == seed.Z,
            "Even Face mask did not remain one voxel layer thick.");

    SmartToolRequest sphere = Request(SmartAction::Paint, states, seed, normal, 1);
    sphere.BrushRequest.State.Shape = SmartBrushShape::Sphere;
    sphere.BrushRequest.State.Dimension = SmartBrushDimension::Volume3D;
    const SmartToolPlanPtr spherePlan = Plan(sphere);
    Require(spherePlan->Cells().size() == states.size(),
        "Face geometry incorrectly depended on Brush Shape or Dimension.");
}

void TestAddDepthUsesWholeLockedFaceLayers()
{
    const Position seed{3, 3, 3};
    const Position normal{0, 1, 0};
    const States states = Plane(seed, normal, 3);
    const SmartToolPlanPtr plan = Plan(Request(SmartAction::Add, states, seed,
        normal, 1, 9U, 3));
    Require(plan->Cells().size() == states.size() * 3U &&
            plan->Bounds().Dimensions.Y == 3U,
        "Face Add depth did not extrude the entire locked surface by exact layers.");
    for (const Position support : {Position{2, 3, 2}, Position{3, 3, 3},
             Position{4, 3, 4}})
        for (const int layer : {1, 2, 3})
            Require(HasCell(*plan, {support.X, support.Y + layer, support.Z}),
                "Face Add depth omitted a voxel from the locked surface extrusion.");

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult invalid = controller.ResolvePreview(session,
        Request(SmartAction::Add, states, seed, normal, 1, 9U, 0));
    Require(!invalid.HasPlan() && invalid.Code == SmartBrushResultCode::InvalidRequest,
        "Face Add accepted a zero depth instead of rejecting the invalid request.");
}

void TestProjectedFaceDepthDragAxes()
{
    const ViewportRectangle viewport{0.0F, 0.0F, 240.0F, 160.0F};
    const Vec3 origin{};
    const SmartToolFaceDepthDragAxis xAxis = MakeSmartToolFaceDepthDragAxis(
        origin, {1.0F, 0.0F, 0.0F}, viewport, IdentityMatrix());
    const SmartToolFaceDepthDragAxis yAxis = MakeSmartToolFaceDepthDragAxis(
        origin, {0.0F, 1.0F, 0.0F}, viewport, IdentityMatrix());
    Require(!xAxis.UsesFallback && !yAxis.UsesFallback &&
            xAxis.ScreenDirection.X > 0.99F && yAxis.ScreenDirection.Y < -0.99F,
        "Face X/Y normals did not produce their projected screen drag axes.");
    Require(ResolveSmartToolFaceDepthLayers(xAxis, {48.0F, 0.0F}) == 3 &&
            ResolveSmartToolFaceDepthLayers(xAxis, {-48.0F, 0.0F}) == 1 &&
            ResolveSmartToolFaceDepthLayers(yAxis, {0.0F, -72.0F}) == 4,
        "Face depth did not use signed motion along the locked projected axis.");

    Matrix4 zToScreenX = IdentityMatrix();
    zToScreenX[0] = 0.0F;
    zToScreenX[2] = 1.0F;
    const SmartToolFaceDepthDragAxis zAxis = MakeSmartToolFaceDepthDragAxis(
        origin, {0.0F, 0.0F, 1.0F}, viewport, zToScreenX);
    Require(!zAxis.UsesFallback && zAxis.ScreenDirection.X > 0.99F &&
            ResolveSmartToolFaceDepthLayers(zAxis, {96.0F, 0.0F}) == 5 &&
            ResolveSmartToolFaceDepthLayers(zAxis, {24.0F, 0.0F}) == 2,
        "Face Z depth did not support exact expansion then shrink on screen X.");

    const SmartToolFaceDepthDragAxis fallback = MakeSmartToolFaceDepthDragAxis(
        origin, {0.0F, 0.0F, 1.0F}, viewport, IdentityMatrix());
    Require(fallback.UsesFallback && fallback.ScreenDirection.Y < -0.99F &&
            ResolveSmartToolFaceDepthLayers(fallback, {0.0F, -48.0F}) == 3 &&
            ResolveSmartToolFaceDepthLayers(fallback, {48.0F, 0.0F}) == 1 &&
            ResolveSmartToolFaceDepthLayers(xAxis, {10000.0F, 0.0F}, 1, 64) == 64,
        "Face depth fallback or clamping was not deterministic.");
}

void TestNoChangeAndOutOfBounds()
{
    const Position seed{3, 3, 3};
    const Position normal{1, 0, 0};
    const States samePalette{{seed, {true, 9U}}};
    const SmartToolPlanPtr empty = Plan(Request(SmartAction::Paint, samePalette, seed, normal));
    Require(empty->Cells().size() == 1U &&
            empty->BrushResult().Code == SmartBrushResultCode::Valid &&
            !empty->HasChanges(), "Painting an exposed Face with the same palette must be no-change.");

    const Position edge{6, 3, 3};
    const States edgeStates{{edge, {true, 2U}}};
    const SmartToolPlanPtr clipped = Plan(Request(SmartAction::Add, edgeStates, edge, normal));
    Require(clipped->BrushResult().Code == SmartBrushResultCode::OutOfBounds &&
            clipped->Statistics().Clipped == 1U && !clipped->HasChanges(),
        "An entirely out-of-bounds Face Add was not diagnosed safely.");
}

void TestHiddenSeedIsRejected()
{
    const Position seed{3, 3, 3};
    const Position normal{0, 1, 0};
    const States hidden{{seed, {true, 2U}}, {Add(seed, normal), {true, 4U}}};
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult result = controller.ResolvePreview(session,
        Request(SmartAction::Paint, hidden, seed, normal));
    Require(!result.HasPlan() && result.Code == SmartBrushResultCode::InvalidRequest,
        "A hidden Face seed was accepted as a visible surface.");
}
}

int main()
{
    try
    {
        TestSixNormalsAndOneLayer();
        TestSquareMaskFiltersHolesAndHiddenCells();
        TestSizeAndShapeAreIgnoredForCompleteSurface();
        TestAddDepthUsesWholeLockedFaceLayers();
        TestProjectedFaceDepthDragAxes();
        TestNoChangeAndOutOfBounds();
        TestHiddenSeedIsRejected();
        std::cout << "Smart Tool Face tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
