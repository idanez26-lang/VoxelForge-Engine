#include "BrushEngine/SmartBrushEngine.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

Editor::SmartBrushResult Resolve(
    Editor::SmartBrushState state,
    const Editor::SmartBrushPlacement placement,
    const Asset::Voxel::VoxelDimensions dimensions = {8U, 8U, 8U},
    const std::function<bool(Position)>& occupied =
        [](const Position) { return false; })
{
    return Editor::SmartBrushEngine::Resolve(
        {dimensions, state, placement, occupied});
}

void RequireUnique(const std::vector<Position>& positions)
{
    for (std::size_t first = 0U; first < positions.size(); ++first)
        for (std::size_t second = first + 1U;
             second < positions.size(); ++second)
            Require(positions[first] != positions[second],
                "Smart Brush generated duplicate positions.");
}

void TestVolumeAndStatistics()
{
    Editor::SmartBrushState cube;
    cube.Size = 3;
    const auto result = Resolve(cube, {{3, 3, 3}, {0, 1, 0}},
        {10U, 10U, 10U}, [](const Position position)
        {
            return position == Position{3, 4, 3};
        });
    Require(result.Code == Editor::SmartBrushResultCode::Valid &&
        result.Statistics.Total == 27U && result.Statistics.New == 26U &&
        result.Statistics.Existing == 1U && result.AddablePositions.size() == 26U &&
        result.ExistingPositions.size() == 1U &&
        result.RenderPlan.Mode == Editor::SmartBrushRenderMode::DetailedCells,
        "Volume Cube statistics or overlap plan is incorrect.");
    RequireUnique(result.Positions);

    cube.Shape = Editor::SmartBrushShape::Sphere;
    const auto sphere = Resolve(cube, {{3, 3, 3}, {0, 1, 0}}, {10U, 10U, 10U});
    Require(sphere.Code == Editor::SmartBrushResultCode::Valid &&
        sphere.Statistics.Total == 19U && sphere.Statistics.New == 19U,
        "Volume Sphere plan is incorrect.");
    RequireUnique(sphere.Positions);

    cube.Size = 16;
    const auto largeSphere = Resolve(
        cube, {{0, 0, 0}, {0, 1, 0}}, {32U, 32U, 32U});
    Require(largeSphere.Code == Editor::SmartBrushResultCode::Valid &&
        largeSphere.Statistics.Clipped > 0U &&
        largeSphere.RenderPlan.Mode ==
            Editor::SmartBrushRenderMode::AggregateSphere &&
        largeSphere.RenderPlan.SphereCenter == Position{0, 7, 0} &&
        largeSphere.RenderPlan.SphereRadius == 8U,
        "A clipped large Sphere did not preserve its raw aggregate outline.");
}

void TestSurfaceOrientations()
{
    Editor::SmartBrushState state;
    state.Dimension = Editor::SmartBrushDimension::Surface2D;
    state.Size = 3;
    state.Orientation = Editor::SmartBrushOrientation::Y;
    const auto ySurface = Resolve(state, {{3, 3, 3}, {1, 0, 0}});
    Require(ySurface.Statistics.Total == 9U &&
        std::all_of(ySurface.Positions.begin(), ySurface.Positions.end(),
            [](const Position position) { return position.Y == 3; }),
        "Surface Y orientation is not planar.");
    RequireUnique(ySurface.Positions);
    state.Orientation = Editor::SmartBrushOrientation::X;
    const auto xSurface = Resolve(state, {{3, 3, 3}, {0, 1, 0}});
    Require(std::all_of(xSurface.Positions.begin(), xSurface.Positions.end(),
            [](const Position position) { return position.X == 3; }),
        "Surface X orientation is not planar.");
    RequireUnique(xSurface.Positions);
    state.Orientation = Editor::SmartBrushOrientation::Auto;
    const auto autoSurface = Resolve(state, {{3, 3, 3}, {0, 0, 1}});
    Require(std::all_of(autoSurface.Positions.begin(), autoSurface.Positions.end(),
            [](const Position position) { return position.Z == 3; }),
        "Surface Auto did not follow the placement normal.");
    RequireUnique(autoSurface.Positions);
    const auto zeroNormal = Resolve(state, {{3, 3, 3}, {0, 0, 0}});
    Require(std::all_of(zeroNormal.Positions.begin(), zeroNormal.Positions.end(),
            [](const Position position) { return position.Y == 3; }),
        "Surface Auto with zero normal did not fall back to Y.");
    state.Shape = Editor::SmartBrushShape::Sphere;
    state.Size = 4;
    state.Orientation = Editor::SmartBrushOrientation::X;
    const auto sphereSurface = Resolve(state, {{3, 3, 3}, {0, 1, 0}});
    Require(sphereSurface.Statistics.Total == 12U &&
        std::all_of(sphereSurface.Positions.begin(), sphereSurface.Positions.end(),
            [](const Position position) { return position.X == 3; }),
        "Even Surface Sphere did not use its selected planar orientation.");
    RequireUnique(sphereSurface.Positions);
}

void TestRefusalsAndAdaptivePlan()
{
    Editor::SmartBrushState state;
    state.Dimension = Editor::SmartBrushDimension::Surface2D;
    state.Size = 3;
    const auto clippedSurface = Resolve(state, {{0, 0, 0}, {0, 1, 0}});
    Require(clippedSurface.Code == Editor::SmartBrushResultCode::Valid &&
        clippedSurface.Statistics.Total ==
            clippedSurface.Statistics.New +
                clippedSurface.Statistics.Existing +
                clippedSurface.Statistics.Clipped &&
        clippedSurface.Statistics.Clipped > 0U &&
        !clippedSurface.Positions.empty(),
        "Boundary Surface brush was not clipped into a valid placement.");
    Require(Resolve(state, {{-4, -4, -4}, {0, 1, 0}}).Code ==
            Editor::SmartBrushResultCode::OutOfBounds,
        "A fully outside Surface brush was not refused.");
    state.Shape = Editor::SmartBrushShape::Cylinder;
    Require(Resolve(state, {{3, 3, 3}, {0, 1, 0}}).Code ==
            Editor::SmartBrushResultCode::Unsupported,
        "Prepared unsupported shape was not refused.");
    state.Shape = Editor::SmartBrushShape::Cube;
    state.Mode = Editor::SmartBrushMode::Erase;
    const auto erase = Resolve(state, {{3, 3, 3}, {0, 1, 0}},
        {8U, 8U, 8U}, [](const Position position)
        {
            return position == Position{3, 3, 3};
        });
    Require(erase.Code == Editor::SmartBrushResultCode::Valid &&
        erase.ExistingPositions == std::vector<Position>{{3, 3, 3}} &&
        erase.AddablePositions.size() == erase.Statistics.Total - 1U &&
        erase.RenderPlan.Mode == Editor::SmartBrushRenderMode::DetailedCells,
        "Erase did not resolve the same occupied-cell geometry as preview.");
    state.Mode = Editor::SmartBrushMode::Add;
    state.Dimension = static_cast<Editor::SmartBrushDimension>(99);
    Require(Resolve(state, {{3, 3, 3}, {0, 1, 0}}).Code ==
            Editor::SmartBrushResultCode::InvalidRequest,
        "Invalid enum state was not rejected.");

    state.Dimension = Editor::SmartBrushDimension::Volume3D;
    state.Size = 3;
    Require(Editor::SmartBrushEngine::EstimateTotal(state) == 27U,
        "Volume Cube estimate is incorrect.");
    state.Shape = Editor::SmartBrushShape::Sphere;
    Require(Editor::SmartBrushEngine::EstimateTotal(state) == 19U,
        "Volume Sphere estimate is incorrect.");
    state.Dimension = Editor::SmartBrushDimension::Surface2D;
    state.Size = 4;
    Require(Editor::SmartBrushEngine::EstimateTotal(state) == 12U,
        "Surface Sphere estimate is incorrect.");
    const auto allOccupied = Resolve(state, {{3, 3, 3}, {0, 1, 0}},
        {10U, 10U, 10U}, [](const Position) { return true; });
    Require(allOccupied.Code == Editor::SmartBrushResultCode::Valid &&
        allOccupied.Statistics.New == 0U &&
        allOccupied.Statistics.Existing == allOccupied.Statistics.Total,
        "Fully occupied Smart Brush did not return a non-blocking plan.");

    state.Size = 16;
    state.Dimension = Editor::SmartBrushDimension::Volume3D;
    state.Shape = Editor::SmartBrushShape::Cube;
    const auto box = Resolve(state, {{8, 0, 8}, {0, 1, 0}}, {32U, 32U, 32U});
    Require(box.Statistics.Total == 4096U &&
        box.RenderPlan.Mode == Editor::SmartBrushRenderMode::AggregateBox,
        "Size 16 Cube did not choose aggregate rendering.");
    state.Shape = Editor::SmartBrushShape::Sphere;
    const auto sphere = Resolve(state, {{8, 0, 8}, {0, 1, 0}}, {32U, 32U, 32U});
    Require(sphere.RenderPlan.Mode == Editor::SmartBrushRenderMode::AggregateSphere,
        "Size 16 Sphere did not choose the sphere aggregate.");
}

void TestBoundaryClipping()
{
    Editor::SmartBrushState state;
    state.Size = 3;
    const auto cube = Resolve(state, {{0, 0, 0}, {0, 1, 0}});
    Require(cube.Code == Editor::SmartBrushResultCode::Valid &&
        cube.Statistics.Total == 27U && cube.Statistics.New == 12U &&
        cube.Statistics.Existing == 0U && cube.Statistics.Clipped == 15U &&
        cube.Positions.size() == cube.Statistics.New &&
        cube.ClippedPositions.size() == cube.Statistics.Clipped,
        "Boundary Cube clipping statistics are incorrect.");
    RequireUnique(cube.Positions);
    RequireUnique(cube.ClippedPositions);

    state.Shape = Editor::SmartBrushShape::Sphere;
    const auto sphere = Resolve(state, {{0, 0, 0}, {0, 1, 0}});
    Require(sphere.Code == Editor::SmartBrushResultCode::Valid &&
        sphere.Statistics.Total == 19U && sphere.Statistics.Clipped > 0U &&
        sphere.Statistics.Total == sphere.Statistics.New +
            sphere.Statistics.Existing + sphere.Statistics.Clipped &&
        sphere.Positions.size() == sphere.Statistics.New &&
        sphere.ClippedPositions.size() == sphere.Statistics.Clipped,
        "Boundary Sphere clipping statistics are incorrect.");
    RequireUnique(sphere.Positions);
}

void TestEraseStatistics()
{
    Editor::SmartBrushState state;
    state.Mode = Editor::SmartBrushMode::Erase;
    state.Size = 3;
    const auto result = Resolve(state, {{0, 0, 0}, {0, 1, 0}},
        {3U, 3U, 3U}, [](const Position position)
        {
            return position == Position{0, 0, 0};
        });
    Require(result.Code == Editor::SmartBrushResultCode::Valid &&
        result.Statistics.Total == 27U && result.Statistics.Existing == 1U &&
        result.Statistics.New == 11U && result.Statistics.Clipped == 15U &&
        result.Statistics.Total == result.Statistics.Existing +
            result.Statistics.New + result.Statistics.Clipped,
        "Smart Erase Total/Erased/Ignored/Clipped statistics are inconsistent.");
}
}

int main()
{
    try
    {
        TestVolumeAndStatistics();
        TestSurfaceOrientations();
        TestRefusalsAndAdaptivePlan();
        TestBoundaryClipping();
        TestEraseStatistics();
        std::cout << "Smart Brush Engine tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Smart Brush Engine tests failed: " << exception.what() << '\n';
        return 1;
    }
}
