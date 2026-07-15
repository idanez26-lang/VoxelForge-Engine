#include "VoxelForge/Project/Project.h"

#include <utility>

namespace VoxelForge::Project
{

Project::Project(
    std::string name,
    std::filesystem::path rootPath,
    std::filesystem::path projectFilePath)
    : id_(),
      name_(std::move(name)),
      rootPath_(std::move(rootPath)),
      projectFilePath_(std::move(projectFilePath))
{
}

const Core::UUID& Project::Id() const noexcept
{
    return id_;
}

const std::string& Project::Name() const noexcept
{
    return name_;
}

const std::filesystem::path& Project::RootPath() const noexcept
{
    return rootPath_;
}

const std::filesystem::path& Project::ProjectFilePath() const noexcept
{
    return projectFilePath_;
}

void Project::Rename(std::string newName)
{
    name_ = std::move(newName);
}

} // namespace VoxelForge::Project
