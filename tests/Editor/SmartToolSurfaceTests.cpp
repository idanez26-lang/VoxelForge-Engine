#include "SmartTools/SmartToolController.h"

#include <algorithm>
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

struct PositionHash final
{
    [[nodiscard]] std::size_t operator()(const Position position) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(position.X)) ^
            (static_cast<std::size_t>(position.Y) << 10U) ^
            (static_cast<std::size_t>(position.Z) << 20U);
    }
};

using States = std::unordered_map<Position, SmartToolVoxelState, PositionHash>;

void Require(const bool value, const std::string_view message)
{
    if (!value)
    {
        throw std::runtime_error(std::string(message));
    }
}

SmartToolPlanPtr Plan(const SmartAction action, const States& source,
    const States& extensions, const Position seed, const Position target,
    const Position normal, const SmartToolMode mode = SmartToolMode::SingleVoxel,
    const int size = 1)
{
    SmartToolRequest request;
    request.Geometry = SmartGeometry::Surface;
    request.Mode = mode;
    request.Action = action;
    request.FaceSeed = {seed, normal};
    request.BrushRequest = {{16U, 16U, 16U},
        {SmartBrushShape::Cube, SmartBrushDimension::Volume3D,
            SmartBrushOrientation::Auto, size, 9U, SmartBrushMode::Add},
        {target, normal}, {}};
    request.ReadFaceSupportVoxel = [&source](const Position position)
    {
        const auto value = source.find(position);
        return value == source.end() ? SmartToolVoxelState{} : value->second;
    };
    request.ReadSurfaceExtensionVoxel = [&extensions](const Position position)
    {
        const auto value = extensions.find(position);
        return value == extensions.end() ? SmartToolVoxelState{} : value->second;
    };
    request.ReadVoxel = [&source, &extensions](const Position position)
    {
        if (const auto extension = extensions.find(position);
            extension != extensions.end())
        {
            return extension->second;
        }
        if (const auto value = source.find(position); value != source.end())
        {
            return value->second;
        }
        return SmartToolVoxelState{};
    };
    request.SourceIdentity = 9U;

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult preview = controller.ResolvePreview(session, request);
    Require(preview.HasPlan() && controller.ResolveCommit(session).Plan == preview.Plan,
        "Surface preview and commit must retain the same immutable plan.");
    return preview.Plan;
}

std::size_t ChangeCount(const SmartToolPlan& plan)
{
    return static_cast<std::size_t>(std::count_if(
        plan.Cells().begin(), plan.Cells().end(),
        [](const SmartToolPlanCell& cell) { return cell.HasChange(); }));
}

void AddSurface(States& source, const Position normal, const Position origin,
    const int firstExtent, const int secondExtent)
{
    for (int second = 0; second < secondExtent; ++second)
    {
        for (int first = 0; first < firstExtent; ++first)
        {
            Position position = origin;
            if (normal.X != 0)
            {
                position.Y += first;
                position.Z += second;
            }
            else if (normal.Y != 0)
            {
                position.X += first;
                position.Z += second;
            }
            else
            {
                position.X += first;
                position.Y += second;
            }
            source.emplace(position, SmartToolVoxelState{true, 2U});
        }
    }
}

void ApplyChanges(States& source, const SmartToolPlan& plan)
{
    for (const SmartToolPlanCell& cell : plan.Cells())
    {
        if (!cell.HasChange())
        {
            continue;
        }
        if (cell.After.Exists)
        {
            source.insert_or_assign(cell.WorldPosition, cell.After);
        }
        else
        {
            source.erase(cell.WorldPosition);
        }
    }
}

Position FirstPlanarExtension(const Position origin, const Position normal,
    const int offset) noexcept
{
    if (normal.X != 0)
    {
        return {origin.X, origin.Y + offset, origin.Z};
    }
    return {origin.X + offset, origin.Y, origin.Z};
}

void TestLockedSurfaceAxesAndIslands()
{
    const States none;
    for (const auto [normal, origin] : {
            std::pair{Position{0, 1, 0}, Position{2, 3, 2}},
            std::pair{Position{1, 0, 0}, Position{3, 2, 2}},
            std::pair{Position{0, 0, 1}, Position{2, 2, 3}}})
    {
        States source;
        AddSurface(source, normal, origin, 3, 3);
        const Position island{10, 10, 10};
        source.emplace(island, SmartToolVoxelState{true, 2U});
        const SmartToolPlanPtr paint = Plan(
            SmartAction::Paint, source, none, origin, origin, normal);
        Require(ChangeCount(*paint) == 9U,
            "Surface flood fill crossed an unconnected coplanar island.");
    }
}

void TestAddConnectivityAndLocalBrushes()
{
    States source;
    const Position normal{0, 1, 0};
    const Position seed{2, 3, 2};
    AddSurface(source, normal, seed, 2, 2);
    States extensions;

    const SmartToolPlanPtr first = Plan(
        SmartAction::Add, source, extensions, seed, {4, 3, 2}, normal);
    Require(ChangeCount(*first) == 1U,
        "Surface Add did not extend the connected contour.");
    extensions.emplace(Position{4, 3, 2}, SmartToolVoxelState{true, 9U});
    const SmartToolPlanPtr second = Plan(
        SmartAction::Add, source, extensions, seed, {5, 3, 2}, normal);
    Require(ChangeCount(*second) == 1U,
        "Surface Add did not connect through the accumulated extension.");
    const SmartToolPlanPtr island = Plan(
        SmartAction::Add, source, extensions, seed, {10, 3, 10}, normal);
    Require(!island->HasChanges(),
        "Surface Add accepted a detached island.");

    for (const SmartToolMode mode : {SmartToolMode::CubeBrush,
             SmartToolMode::SphereBrush, SmartToolMode::CylinderBrush})
    {
        const SmartToolPlanPtr footprint = Plan(
            SmartAction::Add, source, extensions, seed, {4, 3, 2}, normal,
            mode, 3);
        Require(footprint->BrushState().Dimension == SmartBrushDimension::Surface2D &&
                footprint->HasChanges(),
            "Surface brush mode did not resolve to a 2D local footprint.");
    }
}

void TestAllFaceNormalsAndSuccessiveGestures()
{
    const States none;
    for (const Position normal : {Position{1, 0, 0}, Position{-1, 0, 0},
             Position{0, 1, 0}, Position{0, -1, 0}, Position{0, 0, 1},
             Position{0, 0, -1}})
    {
        States source;
        const Position seed{6, 6, 6};
        AddSurface(source, normal, seed, 2, 2);
        const Position first = FirstPlanarExtension(seed, normal, 2);
        const SmartToolPlanPtr firstPlan = Plan(
            SmartAction::Add, source, none, seed, first, normal);
        Require(ChangeCount(*firstPlan) == 1U,
            "Surface Add refused a valid extension for a face normal.");
        ApplyChanges(source, *firstPlan);
        Require(source.contains(first),
            "First Surface Add was not applied to the logical source document.");

        // The next gesture starts from the freshly committed voxel. It must
        // lock its new exposed face and extend without retaining old stroke
        // state or accidentally extruding through the normal.
        const Position second = FirstPlanarExtension(seed, normal, 3);
        const SmartToolPlanPtr secondPlan = Plan(
            SmartAction::Add, source, none, first, second, normal);
        Require(ChangeCount(*secondPlan) == 1U,
            "A newly placed Surface voxel could not seed the next gesture.");
        for (const SmartToolPlanCell& cell : secondPlan->Cells())
        {
            if (cell.HasChange())
            {
                Require(cell.WorldPosition.X * normal.X +
                        cell.WorldPosition.Y * normal.Y +
                        cell.WorldPosition.Z * normal.Z ==
                    seed.X * normal.X + seed.Y * normal.Y + seed.Z * normal.Z,
                    "Surface Add extruded into the face normal.");
            }
        }
    }
}

void TestRemovePaintBoundsAndNoChange()
{
    States source;
    const Position normal{0, 1, 0};
    const Position seed{1, 3, 1};
    AddSurface(source, normal, seed, 3, 3);
    // The rear layer is deliberately occupied. Remove must never cross into it.
    source.emplace(Position{1, 2, 1}, SmartToolVoxelState{true, 5U});
    const States extensions;

    const SmartToolPlanPtr remove = Plan(
        SmartAction::Erase, source, extensions, seed, seed, normal);
    Require(ChangeCount(*remove) == 1U &&
            std::ranges::find_if(remove->Cells(), [seed](const SmartToolPlanCell& cell)
            { return cell.HasChange() && cell.WorldPosition == seed; }) !=
                remove->Cells().end(),
        "Surface Remove traversed behind the locked source surface.");

    const SmartToolPlanPtr paint = Plan(
        SmartAction::Paint, source, extensions, seed, seed, normal);
    Require(ChangeCount(*paint) == 9U,
        "Surface Paint did not repaint the complete locked surface.");

    States alreadyPainted = source;
    for (auto& [position, state] : alreadyPainted)
    {
        if (position.Y == seed.Y)
        {
            state.PaletteIndex = 9U;
        }
    }
    Require(!Plan(SmartAction::Paint, alreadyPainted, extensions, seed, seed,
                 normal)->HasChanges(),
        "Surface Paint did not report a NoChange plan.");

    const SmartToolPlanPtr clipped = Plan(
        SmartAction::Add, source, extensions, seed, {0, 3, 0}, normal,
        SmartToolMode::CubeBrush, 3);
    Require(clipped->PreviewDiagnostics().HasOutOfBounds,
        "Surface out-of-bounds footprint did not retain diagnostics.");
}
} // namespace

int main()
{
    try
    {
        TestLockedSurfaceAxesAndIslands();
        TestAddConnectivityAndLocalBrushes();
        TestAllFaceNormalsAndSuccessiveGestures();
        TestRemovePaintBoundsAndNoChange();
        std::cout << "Smart Tool Surface tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
