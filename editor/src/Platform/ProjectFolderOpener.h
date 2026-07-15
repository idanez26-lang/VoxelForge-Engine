#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace VoxelForge::Editor
{

class ProjectFolderOpener
{
public:
    virtual ~ProjectFolderOpener() = default;
    [[nodiscard]] virtual bool Open(
        const std::filesystem::path& folder,
        std::string& error) = 0;
};

[[nodiscard]] std::string BuildProjectFolderUri(
    const std::filesystem::path& absoluteFolder);

[[nodiscard]] std::unique_ptr<ProjectFolderOpener> CreateSDLProjectFolderOpener();

} // namespace VoxelForge::Editor
