#include "EditorCamera.h"
#include "ViewportInput/ViewportCameraInput.h"

#include <array>
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

void Apply(
    EditorCamera& camera,
    const ViewportCameraActions& actions)
{
    if (actions.FocusRequested) camera.Frame(8.0F, 6.0F, 4.0F);
    if (actions.ResetRequested) camera.Reset();
}

void RequirePointerClickDoesNotMoveCamera(
    EditorCamera& camera,
    const std::uint8_t clickCount,
    const std::string_view scenario)
{
    const EditorCameraState before = camera.CaptureState();
    const ViewportCameraActions actions =
        ResolveViewportCameraActions({true, false, false, clickCount});
    Apply(camera, actions);
    Require(!actions.FocusRequested && !actions.ResetRequested,
        std::string(scenario) + " requested a camera command.");
    Require(camera.CaptureState() == before,
        std::string(scenario) + " changed position, rotation, distance, or target.");
}
}

int main()
{
    try
    {
        EditorCamera camera;
        camera.Frame(8.0F, 6.0F, 4.0F);
        camera.Orbit(31.0F, -17.0F);
        camera.Pan(19.0F, -13.0F, 720.0F);
        camera.Zoom(1.0F);

        RequirePointerClickDoesNotMoveCamera(camera, 1U, "Simple click");
        for (const std::string_view surface :
             std::array<std::string_view, 3>{"grid", "voxel", "empty space"})
        {
            RequirePointerClickDoesNotMoveCamera(
                camera, 2U, std::string("Double-click on ") +
                    std::string(surface));
        }
        RequirePointerClickDoesNotMoveCamera(
            camera, 2U, "Double-click while Pencil is active");
        RequirePointerClickDoesNotMoveCamera(
            camera, 2U, "Double-click while Eraser is active");

        const EditorCameraState beforeOrbit = camera.CaptureState();
        camera.Orbit(12.0F, -8.0F);
        const EditorCameraState afterOrbit = camera.CaptureState();
        Require(afterOrbit.RotationDegrees != beforeOrbit.RotationDegrees &&
                afterOrbit.Target == beforeOrbit.Target &&
                afterOrbit.Distance == beforeOrbit.Distance,
            "Orbit no longer changes only the camera rotation.");

        const EditorCameraState beforePan = camera.CaptureState();
        camera.Pan(-14.0F, 7.0F, 720.0F);
        const EditorCameraState afterPan = camera.CaptureState();
        Require(afterPan.Target != beforePan.Target &&
                afterPan.RotationDegrees == beforePan.RotationDegrees &&
                afterPan.Distance == beforePan.Distance,
            "Pan no longer changes only the camera target.");

        const EditorCameraState beforeZoom = camera.CaptureState();
        camera.Zoom(-1.0F);
        const EditorCameraState afterZoom = camera.CaptureState();
        Require(afterZoom.Distance != beforeZoom.Distance &&
                afterZoom.RotationDegrees == beforeZoom.RotationDegrees &&
                afterZoom.Target == beforeZoom.Target,
            "Zoom no longer changes only the camera distance.");

        const ViewportCameraActions focus =
            ResolveViewportCameraActions({true, true, false, 0U});
        Apply(camera, focus);
        Require(focus.FocusRequested && !focus.ResetRequested &&
                camera.GetTarget() == Vec3{},
            "The explicit focus shortcut no longer works.");

        const ViewportCameraActions reset =
            ResolveViewportCameraActions({true, false, true, 0U});
        Apply(camera, reset);
        const EditorCamera defaultCamera;
        Require(!reset.FocusRequested && reset.ResetRequested &&
                camera.CaptureState() == defaultCamera.CaptureState(),
            "The explicit reset shortcut no longer works.");

        const ViewportCameraActions blocked =
            ResolveViewportCameraActions({false, true, true, 2U});
        Require(!blocked.FocusRequested && !blocked.ResetRequested,
            "Text input or a popup requested a focus command.");

        std::cout << "Double-click camera tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Double-click camera tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
