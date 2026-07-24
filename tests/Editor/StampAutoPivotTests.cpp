#include "VoxelStamps/Capture/StampAutoPivotResolver.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

StampBounds Bounds()
{
    return {
        .Minimum = {},
        .Maximum = {.X = 3, .Y = 2, .Z = 1},
        .Dimensions = {.X = 4U, .Y = 3U, .Z = 2U}};
}

void TestOfficialAutoPriority()
{
    StampPivotContext context{.Bounds = Bounds(), .PlacementKind = StampPivotPlacementKind::Surface,
        .SurfaceNormal = {.X = 0, .Y = 1, .Z = 0}};
    StampPivot pivot = ResolveAutoPivot(context);
    Require(pivot.RequestedMode == StampPivotMode::Auto &&
                pivot.ResolvedMode == StampPivotMode::BottomCenter &&
                pivot.LocalPosition == StampFixedPoint{.X = 512, .Y = 0, .Z = 256} &&
                pivot.LocalNormal == StampNormal{.X = 0, .Y = 1, .Z = 0},
        "Top horizontal surfaces must resolve to Bottom Center.");

    context.SurfaceNormal = {.X = -1, .Y = 0, .Z = 0};
    pivot = ResolveAutoPivot(context);
    Require(pivot.ResolvedMode == StampPivotMode::Surface &&
                pivot.LocalPosition == StampFixedPoint{.X = 0, .Y = 384, .Z = 256} &&
                pivot.LocalNormal == StampNormal{.X = -1, .Y = 0, .Z = 0},
        "Vertical surfaces must resolve to their deterministic attachment face.");

    context.HasSurfaceAttachment = true;
    context.SurfaceAttachment = {.X = 0, .Y = 128, .Z = 512};
    pivot = ResolveAutoPivot(context);
    Require(pivot.ResolvedMode == StampPivotMode::Surface &&
                pivot.LocalPosition == context.SurfaceAttachment,
        "A valid recorded local surface attachment must be preserved exactly.");
    context.HasSurfaceAttachment = false;

    context.SurfaceNormal = {.X = 1, .Y = 1, .Z = 0};
    pivot = ResolveAutoPivot(context);
    Require(pivot.ResolvedMode == StampPivotMode::Center &&
                pivot.LocalPosition == StampFixedPoint{.X = 512, .Y = 384, .Z = 256},
        "Ambiguous surfaces must fall back to Center.");

    context.PlacementKind = StampPivotPlacementKind::Free;
    pivot = ResolveAutoPivot(context);
    Require(pivot.ResolvedMode == StampPivotMode::Center,
        "Free placement must resolve to Center.");

    context.PlacementKind = StampPivotPlacementKind::Workplane;
    context.WorkplaneNormal = {.X = 0, .Y = 1, .Z = 0};
    pivot = ResolveAutoPivot(context);
    Require(pivot.ResolvedMode == StampPivotMode::BottomCenter,
        "A top horizontal Workplane must resolve to Bottom Center.");

    context.PlacementKind = StampPivotPlacementKind::PreciseGrid;
    context.GridDirection = {.X = 1, .Y = -1, .Z = 0};
    pivot = ResolveAutoPivot(context);
    Require(pivot.ResolvedMode == StampPivotMode::Corner &&
                pivot.LocalPosition == StampFixedPoint{.X = 1024, .Y = 0, .Z = 0},
        "Precise-grid placement must resolve to the signed deterministic corner.");
}

void TestOverrideAndDeterminism()
{
    StampPivotContext context{
        .Bounds = Bounds(),
        .RequestedMode = StampPivotMode::BottomCenter,
        .PlacementKind = StampPivotPlacementKind::PreciseGrid,
        .GridDirection = {.X = 1, .Y = 1, .Z = 1}};
    const StampPivot overridden = ResolveAutoPivot(context);
    Require(overridden.ResolvedMode == StampPivotMode::BottomCenter &&
                overridden.LocalPosition == StampFixedPoint{.X = 512, .Y = 0, .Z = 256},
        "An explicit preset must override precise-grid Auto behavior.");

    context.RequestedMode = StampPivotMode::Auto;
    const StampPivot first = ResolveAutoPivot(context);
    const StampPivot second = ResolveAutoPivot(context);
    Require(first == second && first.AutoPolicyVersion == 1U,
        "The same canonical context must always produce the same policy result.");

    context.RequestedMode = StampPivotMode::Surface;
    context.PlacementKind = StampPivotPlacementKind::Free;
    context.SurfaceNormal = {.X = 1, .Y = 1, .Z = 0};
    context.WorkplaneNormal = {};
    const StampPivot invalidSurface = ResolveAutoPivot(context);
    Require(invalidSurface.RequestedMode == StampPivotMode::Surface &&
                invalidSurface.ResolvedMode == StampPivotMode::Center &&
                invalidSurface.LocalNormal == StampNormal{},
        "An explicit Surface override with no principal normal must deterministically fall back to Center.");
}

} // namespace

int main()
{
    try
    {
        TestOfficialAutoPriority();
        TestOverrideAndDeterminism();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
