#include "Constraints/ConstraintEngine.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

bool Near(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 0.0001F;
}

bool Near(const Vec3 left, const Vec3 right) noexcept
{
    return Near(left.X, right.X) && Near(left.Y, right.Y) &&
        Near(left.Z, right.Z);
}

ConstraintResult SolveGrid(
    const Vec3 position, const GridConstraintStep step)
{
    ConstraintRequest request;
    request.Transform.Position = position;
    request.Settings.GridEnabled = true;
    request.Settings.GridStep = step;
    return ConstraintEngine::Solve(request);
}

ConstraintResult SolveRotation(
    const Vec3 rotation, const RotationConstraintStep step)
{
    ConstraintRequest request;
    request.Transform.RotationDegrees = rotation;
    request.Settings.RotationEnabled = true;
    request.Settings.RotationStep = step;
    return ConstraintEngine::Solve(request);
}

void TestGridValues()
{
    Require(Near(SolveGrid(
            {1.12F, 1.13F, -1.13F}, GridConstraintStep::Quarter)
            .Transform.Position,
            {1.0F, 1.25F, -1.25F}),
        "Quarter-cell grid snapping is incorrect.");
    Require(Near(SolveGrid(
            {1.24F, 1.26F, -1.26F}, GridConstraintStep::Half)
            .Transform.Position,
            {1.0F, 1.5F, -1.5F}),
        "Half-cell grid snapping is incorrect.");

    struct GridCase final
    {
        GridConstraintStep Step;
        float Value;
    };
    constexpr std::array<GridCase, 7U> cases{{
        {GridConstraintStep::One, 1.0F},
        {GridConstraintStep::Two, 2.0F},
        {GridConstraintStep::Four, 4.0F},
        {GridConstraintStep::Eight, 8.0F},
        {GridConstraintStep::Sixteen, 16.0F},
        {GridConstraintStep::ThirtyTwo, 32.0F},
        {GridConstraintStep::SixtyFour, 64.0F}}};
    for (const GridCase& test : cases)
    {
        const ConstraintResult result = SolveGrid(
            {test.Value * 1.6F, test.Value * -1.6F, 0.0F}, test.Step);
        Require(Near(result.Transform.Position,
                    {test.Value * 2.0F, test.Value * -2.0F, 0.0F}),
            "An advertised grid step produced the wrong result.");
    }
}

void TestGridEdgeCases()
{
    const ConstraintResult zero = SolveGrid(
        {0.0F, 0.00001F, -0.00001F}, GridConstraintStep::One);
    Require(zero.Transform.Position == Vec3{} &&
            !std::signbit(zero.Transform.Position.Z),
        "Zero and near-zero positions were not normalized deterministically.");

    const ConstraintResult large = SolveGrid(
        {1'000'000.4F, -1'000'000.4F, 123'456.6F},
        GridConstraintStep::One);
    Require(Near(large.Transform.Position,
                {1'000'000.0F, -1'000'000.0F, 123'457.0F}),
        "Large coordinates were not constrained correctly.");
    Require(large.PositionChanged && !large.RotationChanged,
        "Grid change reporting is incorrect.");
}

void TestRotationValues()
{
    struct RotationCase final
    {
        RotationConstraintStep Step;
        float Degrees;
    };
    constexpr std::array<RotationCase, 6U> cases{{
        {RotationConstraintStep::Degrees5, 5.0F},
        {RotationConstraintStep::Degrees10, 10.0F},
        {RotationConstraintStep::Degrees15, 15.0F},
        {RotationConstraintStep::Degrees30, 30.0F},
        {RotationConstraintStep::Degrees45, 45.0F},
        {RotationConstraintStep::Degrees90, 90.0F}}};
    for (const RotationCase& test : cases)
    {
        const ConstraintResult result = SolveRotation(
            {test.Degrees * 1.6F, test.Degrees * -1.4F, 0.0F},
            test.Step);
        Require(Near(result.Transform.RotationDegrees,
                    {test.Degrees * 2.0F, -test.Degrees, 0.0F}),
            "An advertised rotation step produced the wrong result.");
        Require(!result.PositionChanged && result.RotationChanged,
            "Rotation change reporting is incorrect.");
    }
}

void TestDisabledPassThrough()
{
    ConstraintRequest request;
    request.Transform = {
        {1.25F, -2.5F, 0.125F},
        {7.0F, -13.0F, 181.0F},
        {2.0F, 3.0F, 4.0F}};
    request.Settings.GridStep = GridConstraintStep::SixtyFour;
    request.Settings.RotationStep = RotationConstraintStep::Degrees90;
    const ConstraintResult result = ConstraintEngine::Solve(request);
    Require(result.Transform == request.Transform && !result.Changed(),
        "Disabled constraints did not preserve the original transform.");
}

void TestIndependentComponentsAndValueTypes()
{
    ConstraintRequest request;
    request.Transform = {
        {1.4F, 2.6F, -3.4F},
        {14.0F, 16.0F, -44.0F},
        {1.5F, 2.5F, 3.5F}};
    request.Settings.GridEnabled = true;
    request.Settings.GridStep = GridConstraintStep::One;
    request.Settings.RotationEnabled = true;
    request.Settings.RotationStep = RotationConstraintStep::Degrees15;
    const ConstraintResult result = ConstraintEngine::Solve(request);
    Require(Near(result.Transform.Position, {1.0F, 3.0F, -3.0F}) &&
            Near(result.Transform.RotationDegrees,
                {15.0F, 15.0F, -45.0F}) &&
            result.Transform.Scale == request.Transform.Scale,
        "Constraint families did not remain component-isolated.");
    static_assert(std::is_trivially_copyable_v<ConstraintRequest>);
    static_assert(std::is_trivially_copyable_v<ConstraintResult>);
}
}

int main()
{
    try
    {
        TestGridValues();
        TestGridEdgeCases();
        TestRotationValues();
        TestDisabledPassThrough();
        TestIndependentComponentsAndValueTypes();
        std::cout << "Constraint engine tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Constraint engine tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
