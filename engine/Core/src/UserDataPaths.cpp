#include "VoxelForge/Core/UserDataPaths.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace VoxelForge::Core
{

namespace
{

std::filesystem::path NormalizeRoot(std::filesystem::path root)
{
    return root.empty() ? std::filesystem::path{} : root.lexically_normal();
}

} // namespace

UserDataPaths::UserDataPaths(
    std::filesystem::path configurationRoot,
    std::filesystem::path localDataRoot)
    : configurationRoot_(NormalizeRoot(std::move(configurationRoot))),
      localDataRoot_(NormalizeRoot(std::move(localDataRoot)))
{
}

UserDataPaths UserDataPaths::FromSystemEnvironment()
{
#if defined(_WIN32)
    return UserDataPaths(
        PathFromEnvironment("APPDATA").value_or(std::filesystem::path{}),
        PathFromEnvironment("LOCALAPPDATA").value_or(std::filesystem::path{}));
#else
    std::filesystem::path configurationRoot;
    std::filesystem::path localDataRoot;
    const auto home = PathFromEnvironment("HOME");

    if (const auto xdgConfiguration = PathFromEnvironment("XDG_CONFIG_HOME"))
        configurationRoot = *xdgConfiguration;
    else if (home)
        configurationRoot = *home / ".config";

    if (const auto xdgData = PathFromEnvironment("XDG_DATA_HOME"))
        localDataRoot = *xdgData;
    else if (home)
        localDataRoot = *home / ".local" / "share";

    return UserDataPaths(
        std::move(configurationRoot),
        std::move(localDataRoot));
#endif
}

std::optional<std::filesystem::path> UserDataPaths::PathFromEnvironment(
    const std::string_view name)
{
    if (name.empty()) return std::nullopt;

#if defined(_WIN32)
    const std::wstring wideName(name.begin(), name.end());
    wchar_t* value = nullptr;
    std::size_t length = 0U;
    if (_wdupenv_s(&value, &length, wideName.c_str()) != 0 ||
        value == nullptr)
    {
        std::free(value);
        return std::nullopt;
    }
    const std::unique_ptr<wchar_t, decltype(&std::free)> ownedValue(
        value,
        &std::free);
    if (length <= 1U) return std::nullopt;
    return std::filesystem::path(ownedValue.get());
#else
    const std::string variableName(name);
    const char* const value = std::getenv(variableName.c_str());
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
#endif
}

const std::filesystem::path& UserDataPaths::ConfigurationRoot() const noexcept
{
    return configurationRoot_;
}

const std::filesystem::path& UserDataPaths::LocalDataRoot() const noexcept
{
    return localDataRoot_;
}

std::filesystem::path UserDataPaths::ConfigurationDirectory() const
{
    if (configurationRoot_.empty()) return {};

    // Keep the historical directory name so existing preferences and recent
    // projects remain visible. A future migration can move it explicitly.
    return (configurationRoot_ / "VoxelForgeStudio").lexically_normal();
}

std::filesystem::path UserDataPaths::LocalDataDirectory() const
{
    if (localDataRoot_.empty()) return {};
    return (localDataRoot_ / "VoxelForge Studio").lexically_normal();
}

} // namespace VoxelForge::Core
