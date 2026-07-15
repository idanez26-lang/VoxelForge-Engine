#pragma once

#include "VoxelForge/Project/Project.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace VoxelForge::Project
{

struct ProjectFileData
{
    std::string Name;
    std::filesystem::path RootPath;
};

class ProjectSerializer final
{
public:
    static constexpr std::uint32_t FormatVersion = 1;

    [[nodiscard]] static bool Save(
        const Project& project,
        std::string& error);

    [[nodiscard]] static std::optional<ProjectFileData> Load(
        const std::filesystem::path& projectFilePath,
        std::string& error);
};

} // namespace VoxelForge::Project
