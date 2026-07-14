#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace VoxelForge::Core
{

struct ApplicationSpecification
{
    std::string Name = "VoxelForge Editor";
    std::filesystem::path WorkingDirectory{};

    std::uint32_t WindowWidth = 1280;
    std::uint32_t WindowHeight = 720;
    bool WindowResizable = true;
    bool WindowMaximized = false;
    bool PauseOnExit = false;
};

} // namespace VoxelForge::Core
