#include "SmartTools/SmartToolController.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

struct Hash final
{
    std::size_t operator()(const Position value) const noexcept
    {
        return static_cast<std::uint32_t>(value.X) ^
            (static_cast<std::size_t>(
                static_cast<std::uint32_t>(value.Y)) << 21U) ^
            (static_cast<std::size_t>(
                static_cast<std::uint32_t>(value.Z)) << 42U);
    }
};
using States = std::unordered_map<Position, SmartToolVoxelState, Hash>;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

SmartToolRequest Request(const States& states, const SmartFillMode mode,
    const SmartAction action, const Position seed,
    const std::optional<Position> normal = std::nullopt,
    const std::uint8_t targetPalette = 9U,
    const Asset::Voxel::VoxelDimensions dimensions = {16U, 16U, 16U})
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Fill;
    request.FillMode = mode;
    request.Action = action;
    request.BrushRequest = {dimensions,
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, 1, targetPalette,
            SmartBrushMode::Paint},
        {seed, normal.value_or(Position{0, 1, 0})}, {}};
    request.ReadVoxel = [&states](const Position position)
    {
        const auto found = states.find(position);
        return found == states.end() ? SmartToolVoxelState{} : found->second;
    };
    if (mode == SmartFillMode::Plane && normal)
        request.FaceSeed = SmartToolFaceSeed{seed, *normal};
    request.SourceIdentity = 0xF111U;
    request.SourceRevision = 7U;
    return request;
}

SmartToolPlanPtr PreviewAndCommit(const SmartToolRequest& request)
{
    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult preview =
        controller.ResolvePreview(session, request);
    Require(preview.HasPlan(), "Fill preview did not produce a plan.");
    Require(controller.ResolveCommit(session).Plan == preview.Plan,
        "Fill Preview and Commit did not retain the same plan.");
    return preview.Plan;
}

void TestConnectedRegionsAndActions()
{
    const Position seed{2, 2, 2};
    States source{
        {seed, {true, 4U}},
        {{3, 2, 2}, {true, 4U}},
        {{3, 3, 2}, {true, 4U}},
        {{3, 3, 3}, {true, 4U}},
        {{1, 1, 2}, {true, 4U}},
        {{1, 2, 2}, {true, 5U}}};

    const SmartToolPlanPtr paint = PreviewAndCommit(Request(
        source, SmartFillMode::Connected, SmartAction::Paint, seed));
    Require(paint->FillMode() == SmartFillMode::Connected &&
            paint->Cells().size() == 4U &&
            paint->Statistics().Changed == 4U,
        "Connected Fill crossed a palette or diagonal boundary.");
    for (const SmartToolPlanCell& cell : paint->Cells())
        Require(cell.WorldPosition != Position{1, 1, 2} &&
                cell.Before.PaletteIndex == 4U &&
                cell.After.PaletteIndex == 9U,
            "Connected Paint resolved an incorrect cell.");

    const SmartToolPlanPtr create = PreviewAndCommit(Request(
        source, SmartFillMode::Connected, SmartAction::Add, seed));
    Require(create->Action() == SmartAction::Add &&
            create->Cells().size() == paint->Cells().size(),
        "Connected Create did not retain the visible Add action.");
    for (const SmartToolPlanCell& cell : create->Cells())
        Require(cell.Operation == SmartToolCellOperation::Paint &&
                cell.After.PaletteIndex == 9U,
            "Connected Create was not materialized exactly as Paint.");

    const SmartToolPlanPtr remove = PreviewAndCommit(Request(
        source, SmartFillMode::Connected, SmartAction::Erase, seed, std::nullopt,
        0U));
    Require(remove->Statistics().Changed == 4U,
        "Connected Remove did not delete the complete region.");
    for (const SmartToolPlanCell& cell : remove->Cells())
        Require(!cell.After.Exists,
            "Connected Remove retained a source voxel.");

    const States empty;
    SmartToolPlanner planner;
    const SmartToolResult invalid = planner.Plan(Request(
        empty, SmartFillMode::Connected, SmartAction::Paint, seed));
    Require(!invalid.HasPlan() &&
            invalid.Code == SmartBrushResultCode::InvalidRequest,
        "Connected Fill accepted an empty target.");
}

std::array<Position, 2U> Tangents(const Position normal)
{
    if (normal.X != 0) return {{{0, 1, 0}, {0, 0, 1}}};
    if (normal.Y != 0) return {{{1, 0, 0}, {0, 0, 1}}};
    return {{{1, 0, 0}, {0, 1, 0}}};
}

Position Add(const Position left, const Position right)
{
    return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
}

void TestPlaneAxesActionsAndOccupiedDestinations()
{
    const Position seed{4, 4, 4};
    for (const Position normal :
        {Position{1, 0, 0}, Position{0, -1, 0}, Position{0, 0, 1}})
    {
        const auto tangents = Tangents(normal);
        const Position first = Add(seed, tangents[0]);
        const Position second = Add(seed, tangents[1]);
        const Position diagonalOnly{seed.X - tangents[0].X - tangents[1].X,
            seed.Y - tangents[0].Y - tangents[1].Y,
            seed.Z - tangents[0].Z - tangents[1].Z};
        States source{
            {seed, {true, 3U}},
            {first, {true, 3U}},
            {second, {true, 3U}},
            {diagonalOnly, {true, 3U}},
            {Add(Add(seed, tangents[0]), tangents[1]), {true, 8U}},
            {Add(first, normal), {true, 6U}}};

        const SmartToolPlanPtr paint = PreviewAndCommit(Request(
            source, SmartFillMode::Plane, SmartAction::Paint, seed, normal));
        Require(paint->Cells().size() == 3U,
            "Plane Paint crossed its palette boundary.");
        for (const SmartToolPlanCell& cell : paint->Cells())
        {
            const Position delta{cell.WorldPosition.X - seed.X,
                cell.WorldPosition.Y - seed.Y,
                cell.WorldPosition.Z - seed.Z};
            Require(delta.X * normal.X + delta.Y * normal.Y +
                    delta.Z * normal.Z == 0,
                "Plane Paint left the locked source layer.");
        }

        const SmartToolPlanPtr add = PreviewAndCommit(Request(
            source, SmartFillMode::Plane, SmartAction::Add, seed, normal));
        Require(add->Cells().size() == 2U,
            "Plane Create did not ignore an occupied destination.");
        for (const SmartToolPlanCell& cell : add->Cells())
            Require(cell.WorldPosition == Add(seed, normal) ||
                    cell.WorldPosition == Add(second, normal),
                "Plane Create used the wrong normal direction.");

        const SmartToolPlanPtr remove = PreviewAndCommit(Request(
            source, SmartFillMode::Plane, SmartAction::Erase, seed, normal,
            0U));
        Require(remove->Cells().size() == 3U &&
                remove->Statistics().Changed == 3U,
            "Plane Remove did not remain on the source layer.");
    }
}

void TestPlaneInvalidTargetsLimitAndDeterminism()
{
    const Position seed{2, 2, 2};
    States source{{seed, {true, 2U}}, {{3, 2, 2}, {true, 2U}}};
    SmartToolRequest missingFace = Request(
        source, SmartFillMode::Plane, SmartAction::Paint, seed);
    SmartToolPlanner planner;
    Require(!planner.Plan(missingFace).HasPlan(),
        "Plane Fill accepted a missing face seed.");
    Require(MakeSmartToolRequestKey(missingFace) !=
            MakeSmartToolRequestKey(Request(source,
                SmartFillMode::Connected, SmartAction::Paint, seed)),
        "Fill mode was omitted from cache identity.");

    States occluded = source;
    occluded.emplace(Position{2, 3, 2}, SmartToolVoxelState{true, 7U});
    Require(!planner.Plan(Request(occluded, SmartFillMode::Plane,
                SmartAction::Paint, seed, Position{0, 1, 0})).HasPlan(),
        "Plane Fill accepted an occluded seed face.");

    SmartToolPlanner firstPlanner;
    SmartToolPlanner secondPlanner;
    const SmartToolRequest deterministic = Request(
        source, SmartFillMode::Connected, SmartAction::Paint, seed);
    SmartToolController cacheController;
    SmartToolSession cacheSession;
    const SmartToolResult cachedFirst =
        cacheController.ResolvePreview(cacheSession, deterministic);
    const SmartToolResult cachedSecond =
        cacheController.ResolvePreview(cacheSession, deterministic);
    Require(cachedFirst.HasPlan() &&
            cachedSecond.Plan == cachedFirst.Plan,
        "Fill did not reuse its cached immutable plan.");
    const SmartToolResult first = firstPlanner.Plan(deterministic);
    const SmartToolResult second = secondPlanner.Plan(deterministic);
    Require(first.HasPlan() && second.HasPlan() &&
            first.Plan->AffectedPositions() ==
                second.Plan->AffectedPositions(),
        "Fill cell ordering is not deterministic.");

    States largeLine;
    for (int x = 0; x < 4096; ++x)
        largeLine.emplace(Position{x, 0, 0}, SmartToolVoxelState{true, 2U});
    const SmartToolResult large = planner.Plan(Request(
        largeLine, SmartFillMode::Connected, SmartAction::Paint, {0, 0, 0},
        std::nullopt, 9U, {4096U, 1U, 1U}));
    Require(large.HasPlan() && large.Plan->Cells().size() == 4096U &&
            large.Plan->Cells().front().WorldPosition == Position{0, 0, 0} &&
            large.Plan->Cells().back().WorldPosition == Position{4095, 0, 0},
        "Large Connected Fill was incomplete or non-deterministic.");

    States line;
    for (int x = 0; x < 17; ++x)
        line.emplace(Position{x, 0, 0}, SmartToolVoxelState{true, 2U});
    SmartToolPlanner limitedPlanner(16U);
    const SmartToolResult limited = limitedPlanner.Plan(Request(
        line, SmartFillMode::Connected, SmartAction::Paint, {0, 0, 0},
        std::nullopt, 9U, {17U, 1U, 1U}));
    Require(!limited.HasPlan() &&
            limited.Code == SmartBrushResultCode::InvalidRequest,
        "Fill safety limit returned a partial plan.");
}

void TestPlaneAddEmptyFootprintIsBounded()
{
    // VF-STAB-01 bug 1: an isolated boundary voxel at X=0 with the locked face
    // pointing off the grid ({-1,0,0}). The Plane region is the seed alone, and
    // its only outward destination leaves the model, so ResolveFill ends with an
    // EMPTY Positions vector. CalculateBounds does `positions.front()` — front()
    // on an empty std::vector is undefined behaviour, which the try/catch in
    // Plan() cannot catch. This must resolve deterministically instead.
    const Position seed{0, 4, 4};
    const States source{{seed, {true, 3U}}};
    SmartToolPlanner planner;
    const SmartToolResult result = planner.Plan(Request(
        source, SmartFillMode::Plane, SmartAction::Add, seed,
        Position{-1, 0, 0}));
    Require(result.Code == SmartBrushResultCode::OutOfBounds,
        "Plane Add off-grid footprint did not report OutOfBounds.");
    Require(!result.HasPlan() || result.Plan->Cells().empty(),
        "Plane Add off-grid footprint produced spurious cells to commit.");
}
}

int main()
{
    try
    {
        TestConnectedRegionsAndActions();
        TestPlaneAxesActionsAndOccupiedDestinations();
        TestPlaneInvalidTargetsLimitAndDeterminism();
        TestPlaneAddEmptyFootprintIsBounded();
        std::cout << "Smart Tool Fill tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
