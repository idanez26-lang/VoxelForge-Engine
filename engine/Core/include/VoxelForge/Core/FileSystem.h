#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace VoxelForge::Core
{
    class FileSystem final
    {
    public:
        [[nodiscard]] static bool Exists(const std::filesystem::path& path);
        [[nodiscard]] static bool CreateDirectories(const std::filesystem::path& path);
        [[nodiscard]] static bool WriteTextFile(const std::filesystem::path& path, std::string_view content);
        [[nodiscard]] static std::string ReadTextFile(const std::filesystem::path& path);
    };
}
