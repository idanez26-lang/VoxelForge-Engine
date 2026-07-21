#include "EditorCamera.h"
#include "ViewportNavigationController.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

bool Near(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 0.001F;
}

bool Near(const Vec3 left, const Vec3 right) noexcept
{
    return Near(left.X, right.X) && Near(left.Y, right.Y) &&
        Near(left.Z, right.Z);
}

ViewportNavigationBounds Bounds(
    const Vec3 minimum,
    const Vec3 maximum) noexcept
{
    return {minimum, maximum, true};
}

void TestFocusSelection()
{
    EditorCamera camera;
    ViewportNavigationController navigation(camera);
    const auto selection = Bounds({2.0F, 1.0F, -4.0F}, {6.0F, 5.0F, 0.0F});
    Require(navigation.FocusSelection(selection) && navigation.IsFocusing(),
        "Focus selection did not start its interpolation.");
    Require(camera.GetTarget() == Vec3{},
        "Focus selection moved the camera before interpolation advanced.");
    navigation.Tick(0.125F);
    Require(camera.GetTarget() != Vec3{} && camera.GetTarget() != Vec3{4.0F, 3.0F, -2.0F},
        "Focus selection was not smoothly interpolated.");
    navigation.Tick(0.125F);
    Require(!navigation.IsFocusing() &&
        Near(camera.GetTarget(), {4.0F, 3.0F, -2.0F}) &&
        camera.GetDistance() > 1.0F,
        "Focus selection did not end on its bounds center.");
    Require(!navigation.FocusSelection({}),
        "Invalid selection bounds started a focus animation.");
}

void TestFrameAllAndExplicitOrbitTarget()
{
    EditorCamera camera;
    ViewportNavigationController navigation(camera);
    const auto scene = Bounds({-20.0F, -5.0F, -10.0F}, {20.0F, 15.0F, 30.0F});
    Require(navigation.FrameAll(scene) && !navigation.IsFocusing() &&
        Near(camera.GetTarget(), {0.0F, 5.0F, 10.0F}),
        "Frame all did not frame the complete scene immediately.");

    const EditorCameraState beforeOrbit = camera.CaptureState();
    navigation.Orbit(24.0F, -12.0F);
    const EditorCameraState afterOrbit = camera.CaptureState();
    Require(afterOrbit.Target == beforeOrbit.Target &&
        afterOrbit.Distance == beforeOrbit.Distance &&
        afterOrbit.RotationDegrees != beforeOrbit.RotationDegrees,
        "Orbit changed the current camera target implicitly.");

    const auto selected = Bounds({3.0F, 4.0F, 5.0F}, {5.0F, 6.0F, 7.0F});
    Require(navigation.FocusSelection(selected),
        "Explicit selection focus did not start.");
    navigation.Tick(0.25F);
    Require(Near(camera.GetTarget(), {4.0F, 5.0F, 6.0F}),
        "Explicit focus did not establish the selected orbit target.");
}

void TestPanAndZoom()
{
    EditorCamera camera;
    ViewportNavigationController navigation(camera);
    const EditorCameraState beforePan = camera.CaptureState();
    navigation.Pan(20.0F, -14.0F, 720.0F);
    const EditorCameraState afterPan = camera.CaptureState();
    Require(afterPan.Target != beforePan.Target &&
        afterPan.Distance == beforePan.Distance,
        "Pan did not translate proportionally to camera distance.");

    EditorCamera distantCamera;
    distantCamera.Zoom(-4.0F);
    ViewportNavigationController distantNavigation(distantCamera);
    const Vec3 distantBeforePan = distantCamera.GetTarget();
    distantNavigation.Pan(20.0F, -14.0F, 720.0F);
    const float nearPanDistance = Length(afterPan.Target - beforePan.Target);
    const float distantPanDistance =
        Length(distantCamera.GetTarget() - distantBeforePan);
    Require(distantPanDistance > nearPanDistance * 1.8F,
        "Pan did not scale with the camera-to-target distance.");

    const float initialDistance = camera.GetDistance();
    navigation.Zoom(1.0F);
    const float firstStep = initialDistance - camera.GetDistance();
    navigation.Zoom(1.0F);
    const float secondStep = initialDistance - firstStep - camera.GetDistance();
    Require(firstStep > secondStep && secondStep > 0.0F,
        "Zoom speed is not proportional to camera distance.");

    for (int index = 0; index < 128; ++index) navigation.Zoom(1.0F);
    Require(Near(camera.GetDistance(), 0.1F),
        "Zoom stopped before the stable camera minimum distance.");
    const Vec3 targetAtMinimum = camera.GetTarget();
    navigation.Zoom(1.0F);
    Require(Near(camera.GetDistance(), 0.1F) &&
        camera.GetTarget() == targetAtMinimum,
        "Zoom crossed its target or changed the camera target at minimum distance.");
}
}

int main()
{
    try
    {
        TestFocusSelection();
        TestFrameAllAndExplicitOrbitTarget();
        TestPanAndZoom();
        std::cout << "Viewport navigation controller tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Viewport navigation controller tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
