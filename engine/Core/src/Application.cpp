#include "VoxelForge/Core/Application.h"

#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Core/Version.h"

#include <iostream>
#include <string>

namespace VoxelForge::Core
{
    bool Application::Initialize()
    {
        if (initialized_)
        {
            Logger::Instance().Warning(
                "Application initialization was requested more than once.");
            return true;
        }

        const std::string version = Version::Current().ToString();

        std::cout << "========================================\n";
        std::cout << "        VoxelForge Engine\n";
        std::cout << "            v" << version << "\n";
        std::cout << "========================================\n\n";

        Logger::Instance().Info("Initializing Core...");
        Logger::Instance().Info("Logger initialized.");
        Logger::Instance().Info("Version system initialized.");
        Logger::Instance().Info("Application initialized.");

        initialized_ = true;
        return true;
    }

    int Application::Run()
    {
        if (!Initialize())
        {
            Logger::Instance().Error("VoxelForge failed to initialize.");
            return 1;
        }

        Logger::Instance().Info("VoxelForge Engine Ready.");

        std::cout << "\nPress Enter to close VoxelForge...\n";
        std::cin.get();

        Shutdown();
        return 0;
    }

    void Application::Shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        Logger::Instance().Info("Shutting down VoxelForge Engine...");
        initialized_ = false;
        Logger::Instance().Info("Shutdown complete.");
    }
}
