#include "VoxelForge/Project/Project.h"
#include <utility>

namespace VoxelForge::Project
{
    Project::Project(std::string name, std::filesystem::path rootPath)
        : id_(), name_(std::move(name)), rootPath_(std::move(rootPath)) {}

    const Core::UUID& Project::Id() const noexcept { return id_; }
    const std::string& Project::Name() const noexcept { return name_; }
    const std::filesystem::path& Project::RootPath() const noexcept { return rootPath_; }
    void Project::Rename(std::string newName) { name_ = std::move(newName); }
}
