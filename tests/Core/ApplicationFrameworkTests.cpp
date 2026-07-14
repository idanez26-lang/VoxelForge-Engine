#include "VoxelForge/Core/Application.h"
#include "VoxelForge/Core/Event/WindowEvent.h"

#include <cassert>

int main()
{
    VoxelForge::Core::ApplicationSpecification specification;
    specification.Name = "VoxelForge Application Test";
    specification.PauseOnExit = false;

    VoxelForge::Core::Application application(specification);

    assert(application.GetSpecification().Name == "VoxelForge Application Test");
    assert(!application.IsRunning());

    VoxelForge::WindowCloseEvent closeEvent;
    application.OnEvent(closeEvent);

    assert(closeEvent.Handled);
    assert(!application.IsRunning());

    return application.Run();
}
