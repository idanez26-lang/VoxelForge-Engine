#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace VoxelForge::Project
{

class RecentProjects final
{
public:
    static constexpr std::size_t MaximumProjectCount = 10;

    explicit RecentProjects(std::filesystem::path storageFilePath = {});

    [[nodiscard]] bool Load();
    [[nodiscard]] bool Add(const std::filesystem::path& projectFilePath);

    [[nodiscard]] const std::vector<std::filesystem::path>& Projects() const noexcept;
    [[nodiscard]] const std::filesystem::path& StorageFilePath() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] static std::filesystem::path DefaultStorageFilePath();

private:
    [[nodiscard]] bool Save();

    std::filesystem::path storageFilePath_;
    std::vector<std::filesystem::path> projects_;
    std::string lastError_;
};

} // namespace VoxelForge::Project
