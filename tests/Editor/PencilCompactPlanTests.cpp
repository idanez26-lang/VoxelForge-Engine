#include "SmartTools/SmartToolPlanner.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using Position = Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

PencilCompactRequest Request(const SmartBrushShape shape, const int size,
    const SmartBrushDimension dimension = SmartBrushDimension::Volume3D,
    const Position normal = {0, 1, 0})
{
    PencilCompactRequest request;
    request.Dimensions = {2048U, 2048U, 2048U};
    request.Brush.Shape = shape;
    request.Brush.Dimension = dimension;
    request.Brush.Orientation = SmartBrushOrientation::Auto;
    request.Brush.Size = size;
    request.Placement = {{1024, 1024, 1024}, normal};
    request.ProfileIdentity = 0xBEEF1234U;
    request.ProfileRevision = 9U;
    return request;
}

std::size_t CountWithoutMaterializing(const PencilCompactPlan& plan)
{
    std::size_t count = 0U;
    Position position;
    auto iterator = plan.Iterate();
    while (iterator.Next(position)) ++count;
    return count;
}

std::vector<Position> FirstPositions(const PencilCompactPlan& plan,
    const std::size_t maximum)
{
    std::vector<Position> positions;
    positions.reserve(maximum);
    Position position;
    auto iterator = plan.Iterate();
    while (positions.size() < maximum && iterator.Next(position))
        positions.push_back(position);
    return positions;
}

std::vector<Position> SortedCompactPositions(const PencilCompactPlan& plan)
{
    std::vector<Position> positions;
    positions.reserve(plan.ExactVoxelCount());
    Position position;
    auto iterator = plan.Iterate();
    while (iterator.Next(position)) positions.push_back(position);
    std::sort(positions.begin(), positions.end(), [](const Position left,
        const Position right)
    {
        return left.X != right.X ? left.X < right.X :
            left.Y != right.Y ? left.Y < right.Y : left.Z < right.Z;
    });
    return positions;
}

void TestCubeCountsBoundsAndZeroMaterialization()
{
    SmartToolPlanner planner;
    for (const int size : {1, 8, 32, 64, 128, 256})
    {
        const PencilCompactPlanResult result = planner.PlanPencilCompact(
            Request(SmartBrushShape::Cube, size));
        Require(result.Code == PencilCompactPlanCode::Valid && result.HasPlan(),
            "Compact Cube planning failed.");
        const std::size_t side = static_cast<std::size_t>(size);
        Require(result.Plan->ExactVoxelCount() == side * side * side,
            "Compact Cube count is not exact.");
        Require(result.Plan->MaterializedPositionCount() == 0U,
            "Compact Cube planning materialized voxel positions.");
        const int minimum = -((size - 1) / 2);
        const int maximum = size / 2;
        // Volume anchors retain the legacy target/normal contract: with +Y,
        // the first layer begins at Target.Y and the other axes stay centred.
        Require(result.Plan->Bounds().Minimum == Position{1024 + minimum, 1024,
                    1024 + minimum} &&
                result.Plan->Bounds().Maximum == Position{1024 + maximum,
                    1024 + size - 1, 1024 + maximum},
            "Compact Cube bounds changed the legacy Pencil placement contract.");
    }
}

void TestSphereCylinderExactIteratorAtScale()
{
    SmartToolPlanner planner;
    for (const SmartBrushShape shape : {SmartBrushShape::Sphere,
             SmartBrushShape::Cylinder})
    {
        for (const SmartBrushDimension dimension : {SmartBrushDimension::Volume3D,
                 SmartBrushDimension::Surface2D})
        {
            for (const int size : {1, 8, 32, 64, 128, 256})
            {
                const PencilCompactPlanResult result = planner.PlanPencilCompact(
                    Request(shape, size, dimension, {0, 0, 1}));
                Require(result.HasPlan() && result.Code == PencilCompactPlanCode::Valid,
                    "Compact Sphere/Cylinder planning failed.");
                Require(result.Plan->MaterializedPositionCount() == 0U,
                    "Compact Sphere/Cylinder planning materialized positions.");
                Require(CountWithoutMaterializing(*result.Plan) ==
                        result.Plan->ExactVoxelCount(),
                    "Compact procedural iterator differs from its exact count.");
            }
        }
    }
}

void TestLegacyExactCompatibilityBelowLegacyCap()
{
    SmartToolPlanner planner;
    constexpr std::array<Position, 6U> normals{{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
        {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (const SmartBrushShape shape : {SmartBrushShape::Cube,
             SmartBrushShape::Sphere, SmartBrushShape::Cylinder})
    {
        for (const SmartBrushDimension dimension : {SmartBrushDimension::Volume3D,
                 SmartBrushDimension::Surface2D})
        {
            for (const int size : {1, 8, 32, 64})
            {
                for (const Position normal : normals)
                {
                    const std::vector<SmartBrushOrientation> orientations =
                        dimension == SmartBrushDimension::Surface2D
                        ? std::vector<SmartBrushOrientation>{
                            SmartBrushOrientation::Auto, SmartBrushOrientation::X,
                            SmartBrushOrientation::Y, SmartBrushOrientation::Z}
                        : std::vector<SmartBrushOrientation>{
                            SmartBrushOrientation::Auto};
                    for (const SmartBrushOrientation orientation : orientations)
                    {
                        PencilCompactRequest compactRequest = Request(shape, size,
                            dimension, normal);
                        compactRequest.Brush.Orientation = orientation;
                        const PencilCompactPlanResult compact =
                            planner.PlanPencilCompact(compactRequest);
                        SmartBrushRequest legacyRequest;
                        legacyRequest.Dimensions = compactRequest.Dimensions;
                        legacyRequest.State = compactRequest.Brush;
                        legacyRequest.Placement = compactRequest.Placement;
                        legacyRequest.MaximumSize = MaximumSmartBrushRequestSize;
                        legacyRequest.IsOccupied = [](const Position) { return false; };
                        const SmartBrushResult legacy = SmartBrushEngine::Resolve(
                            legacyRequest);
                        std::vector<Position> legacyPositions = legacy.Positions;
                        std::sort(legacyPositions.begin(), legacyPositions.end(),
                            [](const Position left, const Position right)
                        {
                            return left.X != right.X ? left.X < right.X :
                                left.Y != right.Y ? left.Y < right.Y :
                                left.Z < right.Z;
                        });
                        Require(compact.HasPlan() && legacy.Code ==
                                SmartBrushResultCode::Valid &&
                                compact.Plan->Bounds().Minimum ==
                                    legacy.RenderPlan.Bounds.Minimum &&
                                compact.Plan->Bounds().Maximum ==
                                    legacy.RenderPlan.Bounds.Maximum &&
                                SortedCompactPositions(*compact.Plan) == legacyPositions,
                            "Compact Pencil positions changed the established exact brush contract.");
                    }
                }
            }
        }
    }
}

void TestInvalidCompactRequestsAreRejected()
{
    SmartToolPlanner planner;
    PencilCompactRequest request = Request(SmartBrushShape::Cube, 1);
    request.Dimensions = {};
    Require(!planner.PlanPencilCompact(request).HasPlan(), "zero dimensions");
    request = Request(SmartBrushShape::Cube, 1);
    request.Placement.Normal = {1, 1, 0};
    Require(!planner.PlanPencilCompact(request).HasPlan(), "normal");
    request = Request(SmartBrushShape::Cube, 1);
    request.Action = SmartAction::Replace;
    Require(!planner.PlanPencilCompact(request).HasPlan(), "action");
    request = Request(SmartBrushShape::Cube, 1);
    request.PaletteIndex = 0U;
    Require(!planner.PlanPencilCompact(request).HasPlan(), "palette zero");
    request.PaletteIndex = 256U;
    Require(!planner.PlanPencilCompact(request).HasPlan(), "palette range");
    request = Request(SmartBrushShape::Cube, 1);
    request.Brush.PreviewMode = static_cast<SmartBrushPreviewMode>(99U);
    Require(!planner.PlanPencilCompact(request).HasPlan(), "preview mode");
}

void TestDeterministicDescriptorAndBoundedCache()
{
    SmartToolPlanner planner;
    PencilCompactRequest request = Request(SmartBrushShape::Sphere, 128,
        SmartBrushDimension::Surface2D, {1, 0, 0});
    const PencilCompactPlanResult first = planner.PlanPencilCompact(request);
    const PencilCompactPlanResult second = planner.PlanPencilCompact(request);
    Require(first.HasPlan() && second.HasPlan() &&
            first.Plan->CacheKey() == second.Plan->CacheKey() &&
            first.Plan->Descriptor() == second.Plan->Descriptor(),
        "Equivalent compact requests did not reuse an immutable descriptor key.");
    Require(first.Plan->PlanId() != second.Plan->PlanId() &&
            planner.PencilCompactFootprintCacheSize() == 1U,
        "Compact requests did not create a fresh immutable plan over one cached footprint.");
    Require(FirstPositions(*first.Plan, 128U) == FirstPositions(*second.Plan, 128U),
        "Compact iterator order is not deterministic.");
    Require(first.Plan->Bounds().Minimum.X == 1024 &&
            first.Plan->Bounds().Maximum.X == 1024,
        "Surface2D Auto orientation did not resolve to the hit face normal.");

    for (std::uint64_t profile = 1U; profile <= 20U; ++profile)
    {
        request.ProfileIdentity = profile;
        (void)planner.PlanPencilCompact(request);
    }
    Require(planner.PencilCompactFootprintCacheSize() <= 16U,
        "Compact footprint cache is not bounded.");
}

void TestOutOfBoundsIsCompactAndDoesNotEnumerate()
{
    SmartToolPlanner planner;
    PencilCompactRequest request = Request(SmartBrushShape::Cylinder, 256);
    request.Dimensions = {64U, 64U, 64U};
    request.Placement.Target = {0, 0, 0};
    const PencilCompactPlanResult result = planner.PlanPencilCompact(request);
    Require(result.Code == PencilCompactPlanCode::OutOfBounds && result.HasPlan() &&
            result.Plan->MaterializedPositionCount() == 0U,
        "Compact out-of-bounds planning performed unnecessary cell materialization.");
}
}

int main()
{
    try
    {
        TestCubeCountsBoundsAndZeroMaterialization();
        TestSphereCylinderExactIteratorAtScale();
        TestLegacyExactCompatibilityBelowLegacyCap();
        TestInvalidCompactRequestsAreRejected();
        TestDeterministicDescriptorAndBoundedCache();
        TestOutOfBoundsIsCompactAndDoesNotEnumerate();
        std::cout << "Pencil compact plan tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Pencil compact plan tests failed: " << exception.what() << '\n';
        return 1;
    }
}
