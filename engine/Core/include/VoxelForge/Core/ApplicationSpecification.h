#pragma once

#include <filesystem>
#include <string>

namespace VoxelForge::Core
{

struct ApplicationSpecification
{
    std::string Name = "VoxelForge Engine";
    std::filesystem::path WorkingDirectory{};
    bool PauseOnExit = true;
};

} // namespace VoxelForge::Core
