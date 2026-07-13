#include "VoxelForge/Core/Application.h"
#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Project/ProjectManager.h"

#include <exception>
#include <filesystem>
#include <iostream>

int main()
{
    try
    {
        VoxelForge::Project::ProjectManager projectManager;

        const auto demoPath =
            std::filesystem::current_path() / "workspace" / "DemoProject";

        const auto project =
            projectManager.CreateProject("DemoProject", demoPath);

        if (!project)
        {
            VoxelForge::Core::Logger::Instance().Error(
                "The demonstration project could not be created.");
            return 1;
        }

        VoxelForge::Core::Application application;
        const int result = application.Run();

        projectManager.CloseProject();
        return result;
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
