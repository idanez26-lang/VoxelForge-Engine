#include "Layout/EditorLayoutPersistence.h"

#include "VoxelForge/Core/UserDataPaths.h"

#include <system_error>
#include <utility>

namespace VoxelForge::Editor
{

namespace
{
std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
}

EditorLayoutPersistence::EditorLayoutPersistence(
    std::filesystem::path pathOverride)
    : pathOverride_(std::move(pathOverride))
{
}

bool EditorLayoutPersistence::Initialize()
{
    if (initialized_) return true;
    lastError_.clear();
    path_ = pathOverride_.empty() ? ResolveCanonicalPath() : pathOverride_;
    if (path_.empty())
    {
        lastError_ = "The local user data directory is unavailable.";
        return false;
    }
    if (!path_.is_absolute())
    {
        lastError_ = "The ImGui layout path must be absolute.";
        return false;
    }
    path_ = path_.lexically_normal();
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error)
    {
        lastError_ = "Unable to create the ImGui layout directory: " +
            error.message();
        return false;
    }
    iniFilenameUtf8_ = PathToUtf8(path_);
    if (iniFilenameUtf8_.empty())
    {
        lastError_ = "Unable to encode the ImGui layout path as UTF-8.";
        return false;
    }
    initialized_ = true;
    return true;
}

bool EditorLayoutPersistence::IsInitialized() const noexcept
{
    return initialized_;
}

const std::filesystem::path& EditorLayoutPersistence::Path() const noexcept
{
    return path_;
}

const char* EditorLayoutPersistence::IniFilename() const noexcept
{
    return initialized_ ? iniFilenameUtf8_.c_str() : nullptr;
}

const std::string& EditorLayoutPersistence::LastError() const noexcept
{
    return lastError_;
}

std::filesystem::path EditorLayoutPersistence::CanonicalPathFromLocalAppData(
    const std::filesystem::path& localAppDataRoot)
{
    const Core::UserDataPaths paths({}, localAppDataRoot);
    const std::filesystem::path directory = paths.LocalDataDirectory();
    return directory.empty()
        ? std::filesystem::path{}
        : directory / "imgui.ini";
}

std::filesystem::path EditorLayoutPersistence::ResolveCanonicalPath()
{
    const std::filesystem::path directory =
        Core::UserDataPaths::FromSystemEnvironment().LocalDataDirectory();
    return directory.empty()
        ? std::filesystem::path{}
        : directory / "imgui.ini";
}

} // namespace VoxelForge::Editor
