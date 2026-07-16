#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace VoxelForge::Editor
{

[[nodiscard]] inline std::filesystem::path DeriveVoxelSavePath(
    const std::filesystem::path& sourcePath)
{
    std::filesystem::path result = sourcePath;
    std::string extension = result.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    if (extension != ".vfvoxel")
    {
        result.replace_extension(".vfvoxel");
    }
    return result;
}

class VoxelSaveState final
{
public:
    void OnModelLoaded(const std::filesystem::path& sourcePath)
    {
        savePath_ = DeriveVoxelSavePath(sourcePath);
        dirty_ = false;
    }

    void MarkModified() noexcept
    {
        dirty_ = true;
    }

    void MarkSaved() noexcept
    {
        dirty_ = false;
    }

    void UpdateFromHistory(const bool isAtSavedState) noexcept
    {
        dirty_ = !isAtSavedState;
    }

    void Clear() noexcept
    {
        savePath_.clear();
        dirty_ = false;
    }

    [[nodiscard]] bool IsDirty() const noexcept
    {
        return dirty_;
    }

    [[nodiscard]] const std::filesystem::path& SavePath() const noexcept
    {
        return savePath_;
    }

private:
    std::filesystem::path savePath_;
    bool dirty_ = false;
};

} // namespace VoxelForge::Editor
