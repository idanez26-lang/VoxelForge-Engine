#include "SmartTools/SmartToolLineConstraintResolver.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;
using Position = VoxelForge::Asset::Voxel::VoxelPosition;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void TestFreeAndConstrainedAxes()
{
    SmartToolLineConstraintResolver resolver;
    const Position pointA{4, 5, 6};
    resolver.Begin(pointA);
    const auto free = resolver.Resolve({9, 8, 7}, false);
    Require(!free.Axis && free.Endpoint == Position{9, 8, 7},
        "Line without Shift was not kept free.");

    const auto check = [&resolver, pointA](const Position endpoint,
        const SmartToolLineAxis axis, const Position expected)
    {
        resolver.Begin(pointA);
        const auto result = resolver.Resolve(endpoint, true);
        Require(result.Axis == axis && result.Endpoint == expected,
            "Shift Line did not resolve to the expected world axis.");
    };
    check({10, 7, 8}, SmartToolLineAxis::X, {10, 5, 6});
    check({1, -2, 4}, SmartToolLineAxis::Y, {4, -2, 6});
    check({2, 3, -3}, SmartToolLineAxis::Z, {4, 5, -3});
    check({-2, 4, 5}, SmartToolLineAxis::X, {-2, 5, 6});
    check({3, -3, 5}, SmartToolLineAxis::Y, {4, -3, 6});
    check({3, 4, 15}, SmartToolLineAxis::Z, {4, 5, 15});
}

void TestDeterministicTieAndHysteresis()
{
    SmartToolLineConstraintResolver resolver;
    resolver.Begin({0, 0, 0});
    const auto tie = resolver.Resolve({4, 4, 1}, true);
    Require(tie.Axis == SmartToolLineAxis::X && tie.Endpoint == Position{4, 0, 0},
        "Line ties must use the documented X then Y then Z priority.");

    const auto nearY = resolver.Resolve({4, 5, 1}, true);
    Require(nearY.Axis == SmartToolLineAxis::X,
        "Line axis switched without exceeding the hysteresis threshold.");
    const auto clearY = resolver.Resolve({4, 6, 1}, true);
    Require(clearY.Axis == SmartToolLineAxis::Y &&
            clearY.Endpoint == Position{0, 6, 0},
        "Line axis did not switch after a clear dominant change.");
}

void TestShiftTransitionsAndReset()
{
    SmartToolLineConstraintResolver resolver;
    resolver.Begin({1, 1, 1});
    const auto pressed = resolver.Resolve({7, 3, 2}, true);
    Require(pressed.Axis == SmartToolLineAxis::X &&
            pressed.Endpoint == Position{7, 1, 1},
        "Pressing Shift after MouseDown did not constrain the line.");
    const auto released = resolver.Resolve({7, 3, 2}, false);
    Require(!released.Axis && released.Endpoint == Position{7, 3, 2} &&
            !resolver.LockedAxis(),
        "Releasing Shift did not restore the free endpoint.");
    resolver.Reset();
    Require(!resolver.IsActive() && !resolver.LockedAxis(),
        "Line constraint state survived cancellation/reset.");
}

void TestZeroDragDoesNotLockTheWrongAxis()
{
    SmartToolLineConstraintResolver resolver;
    const Position pointA{3, 3, 3};
    resolver.Begin(pointA);
    const auto zero = resolver.Resolve(pointA, true);
    Require(!zero.Axis && zero.Endpoint == pointA,
        "Shift MouseDown with no displacement must not lock X.");
    const auto firstY = resolver.Resolve({3, 4, 3}, true);
    Require(firstY.Axis == SmartToolLineAxis::Y &&
            firstY.Endpoint == Position{3, 4, 3},
        "First Shift movement along Y inherited a stale zero-drag lock.");

    resolver.Begin(pointA);
    static_cast<void>(resolver.Resolve(pointA, true));
    const auto firstZ = resolver.Resolve({3, 3, 4}, true);
    Require(firstZ.Axis == SmartToolLineAxis::Z &&
            firstZ.Endpoint == Position{3, 3, 4},
        "First Shift movement along Z inherited a stale zero-drag lock.");
}
}

int main()
{
    try
    {
        TestFreeAndConstrainedAxes();
        TestDeterministicTieAndHysteresis();
        TestShiftTransitionsAndReset();
        TestZeroDragDoesNotLockTheWrongAxis();
        std::cout << "Smart Tool Line constraint resolver tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
