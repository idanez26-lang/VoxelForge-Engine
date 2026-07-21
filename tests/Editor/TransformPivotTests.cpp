#include "Transform/TransformPivotManager.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool Near(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 0.0001F;
}

void RequirePosition(
    const TransformPivotManager& manager,
    const Vec3 expected,
    const std::string_view message)
{
    Require(manager.HasValidPivot(), "Expected a valid transform pivot.");
    const Vec3 actual = manager.GetPivot().WorldPosition;
    Require(Near(actual.X, expected.X) && Near(actual.Y, expected.Y) &&
            Near(actual.Z, expected.Z), message);
}

void TestInitialStateAndUnitBounds()
{
    TransformPivotManager manager;
    Require(manager.GetMode() == TransformPivotMode::Center,
        "Transform pivot mode must initially be Center.");
    Require(!manager.HasValidPivot(),
        "Transform pivot must initially be invalid.");

    const SelectionBounds unit = SelectionBounds::FromCorners(
        {4, 2, 7}, {4, 2, 7});
    Require(manager.UpdateFromBounds(unit),
        "Unit bounds must calculate the initial pivot.");
    RequirePosition(manager, {4.5F, 2.5F, 7.5F},
        "Unit Center must use voxel-cell centers.");

    Require(manager.SetMode(TransformPivotMode::Bottom),
        "Center to Bottom must change the mode.");
    RequirePosition(manager, {4.5F, 2.0F, 7.5F},
        "Unit Bottom must lie on the lower external face.");
    Require(manager.SetMode(TransformPivotMode::Top),
        "Bottom to Top must change the mode.");
    RequirePosition(manager, {4.5F, 3.0F, 7.5F},
        "Unit Top must lie on the upper external face.");
}

void TestAsymmetricBoundsAndWorldTranslation()
{
    const SelectionBounds bounds = SelectionBounds::FromCorners(
        {2, 4, 6}, {8, 10, 14});
    TransformPivotManager manager;
    Require(manager.UpdateFromBounds(bounds),
        "Asymmetric bounds must calculate Center.");
    RequirePosition(manager, {5.5F, 7.5F, 10.5F},
        "Asymmetric Center is incorrect.");

    static_cast<void>(manager.SetMode(TransformPivotMode::Bottom));
    RequirePosition(manager, {5.5F, 4.0F, 10.5F},
        "Asymmetric Bottom is incorrect.");
    static_cast<void>(manager.SetMode(TransformPivotMode::Top));
    RequirePosition(manager, {5.5F, 11.0F, 10.5F},
        "Asymmetric Top is incorrect.");

    static_cast<void>(manager.SetMode(TransformPivotMode::Center));
    Require(manager.UpdateFromBounds(bounds, {1.5F, 2.0F, 3.5F}),
        "A changed model center must recalculate world position.");
    RequirePosition(manager, {4.0F, 5.5F, 7.0F},
        "Pivot world translation must match viewport conventions.");
}

void TestCachingAndInvalidation()
{
    TransformPivotManager manager;
    const SelectionBounds first = SelectionBounds::FromCorners(
        {1, 2, 3}, {5, 8, 13});
    Require(manager.UpdateFromBounds(first),
        "Initial valid bounds must recalculate.");
    Require(!manager.UpdateFromBounds(first),
        "Unchanged bounds and model center must hit the cache.");
    Require(!manager.SetMode(TransformPivotMode::Center),
        "Setting the current mode must not recalculate.");
    Require(manager.SetMode(TransformPivotMode::Bottom),
        "A changed mode must recalculate a valid pivot.");
    Require(!manager.SetMode(TransformPivotMode::Bottom),
        "An unchanged Bottom mode must hit the cache.");

    const SelectionBounds second = SelectionBounds::FromCorners(
        {1, 2, 3}, {7, 8, 13});
    Require(manager.UpdateFromBounds(second),
        "Changed bounds must recalculate the pivot.");
    RequirePosition(manager, {4.5F, 2.0F, 8.5F},
        "Changed bounds produced an incorrect Bottom pivot.");

    manager.Invalidate();
    Require(!manager.HasValidPivot(),
        "Invalidate must make the pivot unavailable.");
    Require(manager.GetMode() == TransformPivotMode::Bottom,
        "Invalidation must preserve the selected mode.");
    Require(manager.UpdateFromBounds(second),
        "Valid bounds after invalidation must recalculate.");
}

void TestInvalidNegativeAndLargeBounds()
{
    TransformPivotManager manager;
    SelectionBounds invalid;
    Require(!manager.UpdateFromBounds(invalid) &&
            !manager.HasValidPivot(),
        "Bounds without the Valid flag must be rejected.");
    invalid = {{5, 0, 0}, {4, 1, 1}, true};
    Require(!manager.UpdateFromBounds(invalid) &&
            !manager.HasValidPivot(),
        "Reversed bounds must be rejected.");

    const SelectionBounds negative = SelectionBounds::FromCorners(
        {-8, -5, -2}, {-2, -1, 4});
    Require(manager.UpdateFromBounds(negative),
        "Negative bounds must remain representable.");
    RequirePosition(manager, {-4.5F, -2.5F, 1.5F},
        "Negative Center is incorrect.");

    const SelectionBounds large = SelectionBounds::FromCorners(
        {1000000, 2000000, 3000000},
        {1000100, 2000200, 3000300});
    static_cast<void>(manager.SetMode(TransformPivotMode::Top));
    Require(manager.UpdateFromBounds(large),
        "Large bounds must remain representable.");
    RequirePosition(manager, {1000050.5F, 2000201.0F, 3000150.5F},
        "Large Top pivot is incorrect.");

    Require(manager.UpdateFromBounds(large, {NAN, 0.0F, 0.0F}) &&
            !manager.HasValidPivot(),
        "A non-finite model center must invalidate the pivot.");
}

void TestNoDocumentMutationOrRendererDependency()
{
    static_assert(std::is_trivially_copyable_v<TransformPivot>);
    static_assert(std::is_nothrow_default_constructible_v<TransformPivotManager>);
    VoxelForge::Asset::Voxel::VoxelDocument document;
    const std::uint64_t revision = document.GetRevision();
    const std::uint64_t voxelCount = document.GetVoxelCount();

    TransformPivotManager manager;
    static_cast<void>(manager.UpdateFromBounds(
        SelectionBounds::FromCorners({0, 0, 0}, {31, 31, 31})));
    static_cast<void>(manager.SetMode(TransformPivotMode::Bottom));
    static_cast<void>(manager.SetMode(TransformPivotMode::Top));
    manager.Invalidate();

    Require(document.GetRevision() == revision &&
            document.GetVoxelCount() == voxelCount && !document.IsDirty(),
        "Pivot calculations must not mutate a voxel document.");
}
}

int main()
{
    try
    {
        TestInitialStateAndUnitBounds();
        TestAsymmetricBoundsAndWorldTranslation();
        TestCachingAndInvalidation();
        TestInvalidNegativeAndLargeBounds();
        TestNoDocumentMutationOrRendererDependency();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
