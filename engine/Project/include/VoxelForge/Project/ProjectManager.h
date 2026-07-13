#pragma once
#include "VoxelForge/Project/Project.h"
#include <filesystem>
#include <memory>
#include <string>

namespace VoxelForge::Project
{
    class ProjectManager final
    {
    public:
        [[nodiscard]] std::shared_ptr<Project> CreateProject(
            std::string name,
            const std::filesystem::path& rootPath);

        [[nodiscard]] const std::shared_ptr<Project>& ActiveProject() const noexcept;
        void CloseProject() noexcept;

    private:
        std::shared_ptr<Project> activeProject_;
    };
}
