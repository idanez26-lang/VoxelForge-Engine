#include "VoxelForge/Core/FileSystem.h"
#include <fstream>
#include <sstream>

namespace VoxelForge::Core
{
    bool FileSystem::Exists(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::exists(path, error) && !error;
    }

    bool FileSystem::CreateDirectories(const std::filesystem::path& path)
    {
        std::error_code error;
        if (std::filesystem::exists(path, error))
            return !error;
        return std::filesystem::create_directories(path, error) && !error;
    }

    bool FileSystem::WriteTextFile(const std::filesystem::path& path, std::string_view content)
    {
        const auto parent = path.parent_path();
        if (!parent.empty() && !CreateDirectories(parent))
            return false;

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        return static_cast<bool>(file);
    }

    std::string FileSystem::ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }
}
