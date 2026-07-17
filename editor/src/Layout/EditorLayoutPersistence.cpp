#include "Layout/EditorLayoutPersistence.h"

#include <cstdlib>
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
        lastError_ = "LOCALAPPDATA is unavailable.";
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
    if (localAppDataRoot.empty()) return {};
    return (localAppDataRoot / "VoxelForge Studio" / "imgui.ini")
        .lexically_normal();
}

std::filesystem::path EditorLayoutPersistence::ResolveCanonicalPath()
{
#if defined(_WIN32)
    char* localAppData = nullptr;
    std::size_t length = 0U;
    if (_dupenv_s(&localAppData, &length, "LOCALAPPDATA") != 0 ||
        localAppData == nullptr || length <= 1U)
    {
        std::free(localAppData);
        return {};
    }
    const std::filesystem::path result = CanonicalPathFromLocalAppData(
        std::filesystem::path(localAppData));
    std::free(localAppData);
    return result;
#else
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData == nullptr || *localAppData == '\0') return {};
    return CanonicalPathFromLocalAppData(
        std::filesystem::path(localAppData));
#endif
}

} // namespace VoxelForge::Editor
