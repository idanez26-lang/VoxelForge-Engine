#include "EditorLayer.h"
#include "EditorWindowTitle.h"

#include "VoxelForge/Core/Application.h"
#include "VoxelForge/Core/ApplicationSpecification.h"
#include "VoxelForge/Core/Window/Window.h"
#include "VoxelForge/Project/ProjectManager.h"

#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{

constexpr std::size_t SmokeTestFrameCount = 5;

bool IsSmokeTestRequested(const int argumentCount, char* arguments[])
{
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::string_view(arguments[index]) == "--smoke-test")
        {
            return true;
        }
    }

    return false;
}

} // namespace

int main(const int argumentCount, char* arguments[])
{
    try
    {
        const bool smokeTestRequested =
            IsSmokeTestRequested(argumentCount, arguments);

        VoxelForge::Project::ProjectManager projectManager;

        VoxelForge::Core::ApplicationSpecification applicationSpecification;
        applicationSpecification.Name =
            VoxelForge::Editor::FormatEditorWindowTitle();
        VoxelForge::Core::Application application(
            std::move(applicationSpecification));
        application.PushLayer(
            std::make_unique<VoxelForge::Editor::EditorLayer>(
                projectManager,
                [&application](std::string title)
                {
                    return application.GetWindow().SetTitle(std::move(title));
                },
                [&application]() noexcept
                {
                    application.Close();
                },
                smokeTestRequested ? SmokeTestFrameCount : 0));

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
