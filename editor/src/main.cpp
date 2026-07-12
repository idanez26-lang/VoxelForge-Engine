#include "VoxelForge/Core/Application.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        VoxelForge::Core::Application application;
        return application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FATAL] Unhandled exception: "
                  << exception.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "[FATAL] Unknown unhandled exception.\n";
        return 1;
    }
}
