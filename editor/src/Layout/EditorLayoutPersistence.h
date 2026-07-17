#pragma once

#include <filesystem>
#include <string>

namespace VoxelForge::Editor
{

class EditorLayoutPersistence final
{
public:
    explicit EditorLayoutPersistence(
        std::filesystem::path pathOverride = {});

    [[nodiscard]] bool Initialize();
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept;
    [[nodiscard]] const char* IniFilename() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] static std::filesystem::path CanonicalPathFromLocalAppData(
        const std::filesystem::path& localAppDataRoot);
    [[nodiscard]] static std::filesystem::path ResolveCanonicalPath();

private:
    std::filesystem::path pathOverride_;
    std::filesystem::path path_;
    std::string iniFilenameUtf8_;
    std::string lastError_;
    bool initialized_ = false;
};

} // namespace VoxelForge::Editor
