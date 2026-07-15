#pragma once

#include "VoxelForge/Project/Project.h"
#include "VoxelForge/Project/RecentProjects.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Project
{

class ProjectManager final
{
public:
    explicit ProjectManager(
        std::filesystem::path recentProjectsFilePath =
            RecentProjects::DefaultStorageFilePath());

    [[nodiscard]] std::shared_ptr<Project> CreateProject(
        std::string name,
        const std::filesystem::path& parentDirectory);

    [[nodiscard]] std::shared_ptr<Project> OpenProject(
        const std::filesystem::path& projectFilePath);

    [[nodiscard]] bool SaveActiveProject();
    void CloseProject() noexcept;

    [[nodiscard]] bool HasActiveProject() const noexcept;
    [[nodiscard]] const std::shared_ptr<Project>& ActiveProject() const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& RecentProjectPaths() const noexcept;
    [[nodiscard]] const std::filesystem::path& RecentProjectsFilePath() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

private:
    void RecordRecentProject(const std::filesystem::path& projectFilePath);
    void SetError(std::string error);

    std::shared_ptr<Project> activeProject_;
    RecentProjects recentProjects_;
    std::string lastError_;
};

} // namespace VoxelForge::Project
