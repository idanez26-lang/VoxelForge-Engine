#pragma once

#include <filesystem>
#include <string>

namespace VoxelForge::Editor
{

class ProjectDialogPreferences final
{
public:
    explicit ProjectDialogPreferences(std::filesystem::path storageFilePath);

    [[nodiscard]] bool Load();
    [[nodiscard]] bool SetLastCreateParent(std::filesystem::path path);
    [[nodiscard]] bool SetLastOpenDirectory(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& LastCreateParent() const noexcept;
    [[nodiscard]] const std::filesystem::path& LastOpenDirectory() const noexcept;
    [[nodiscard]] const std::filesystem::path& StorageFilePath() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] static std::filesystem::path DefaultStorageFilePath();

private:
    [[nodiscard]] bool Save();
    static std::filesystem::path ExistingDirectoryOrEmpty(
        const std::filesystem::path& path);

    std::filesystem::path storageFilePath_;
    std::filesystem::path lastCreateParent_;
    std::filesystem::path lastOpenDirectory_;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
