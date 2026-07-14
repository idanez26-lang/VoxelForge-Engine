#pragma once

#include <array>
#include <string>
#include <vector>

namespace VoxelForge::Scene
{

struct TransformComponent
{
    std::array<float, 3> Position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> Rotation{0.0F, 0.0F, 0.0F};
    std::array<float, 3> Scale{1.0F, 1.0F, 1.0F};
};

struct MetadataComponent
{
    std::string Author = "Tony";
    std::string Category = "Generic";
    std::vector<std::string> Tags{};
};

struct ForgeDNAComponent
{
    std::string Style = "Default";
    std::string Palette = "Default";
    std::string State = "New";
    std::string Purpose = "Decoration";
};

} // namespace VoxelForge::Scene
