#include "VoxelForge/Project/ProjectManager.h"

#include "VoxelForge/Core/FileSystem.h"
#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Core/Time.h"

#include <sstream>
#include <utility>

namespace VoxelForge::Project
{
    std::shared_ptr<Project> ProjectManager::CreateProject(
        std::string name,
        const std::filesystem::path& rootPath)
    {
        if (!Core::FileSystem::CreateDirectories(rootPath))
        {
            Core::Logger::Instance().Error("Unable to create the project directory.");
            return {};
        }

        auto project = std::make_shared<Project>(std::move(name), rootPath);

        std::ostringstream metadata;
        metadata << "{\n"
                 << "  \"name\": \"" << project->Name() << "\",\n"
                 << "  \"id\": \"" << project->Id().ToString() << "\",\n"
                 << "  \"createdAt\": \"" << Core::Time::NowIso8601() << "\",\n"
                 << "  \"formatVersion\": 1\n"
                 << "}\n";

        if (!Core::FileSystem::WriteTextFile(rootPath / "project.json", metadata.str()))
        {
            Core::Logger::Instance().Error("Unable to write the project metadata file.");
            return {};
        }

        activeProject_ = project;
        Core::Logger::Instance().Info("Project created: " + activeProject_->Name());
        return activeProject_;
    }

    const std::shared_ptr<Project>& ProjectManager::ActiveProject() const noexcept
    {
        return activeProject_;
    }

    void ProjectManager::CloseProject() noexcept
    {
        if (activeProject_)
            Core::Logger::Instance().Info("Project closed: " + activeProject_->Name());

        activeProject_.reset();
    }
}
