#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace VoxelForge::Core
{

class UserDataPaths final
{
public:
    UserDataPaths(
        std::filesystem::path configurationRoot,
        std::filesystem::path localDataRoot);

    [[nodiscard]] static UserDataPaths FromSystemEnvironment();
    [[nodiscard]] static std::optional<std::filesystem::path>
        PathFromEnvironment(std::string_view name);

    [[nodiscard]] const std::filesystem::path& ConfigurationRoot() const noexcept;
    [[nodiscard]] const std::filesystem::path& LocalDataRoot() const noexcept;
    [[nodiscard]] std::filesystem::path ConfigurationDirectory() const;
    [[nodiscard]] std::filesystem::path LocalDataDirectory() const;

private:
    std::filesystem::path configurationRoot_;
    std::filesystem::path localDataRoot_;
};

} // namespace VoxelForge::Core
