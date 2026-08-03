#include "VoxelForge/Core/UserDataPaths.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{

bool Expect(const bool condition, const std::string_view message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    using VoxelForge::Core::UserDataPaths;

    const fs::path root =
        (fs::temp_directory_path() / "VoxelForgeUserDataPathsTests")
            .lexically_normal();
    const fs::path configurationRoot = root / "Roaming";
    const fs::path localDataRoot = root / "Local";
    const UserDataPaths paths(configurationRoot, localDataRoot);

    if (!Expect(paths.ConfigurationRoot() == configurationRoot,
            "The injected configuration root was not preserved.") ||
        !Expect(paths.LocalDataRoot() == localDataRoot,
            "The injected local-data root was not preserved.") ||
        !Expect(paths.ConfigurationDirectory() ==
                configurationRoot / "VoxelForgeStudio",
            "The compatible configuration directory is incorrect.") ||
        !Expect(paths.LocalDataDirectory() ==
                localDataRoot / "VoxelForge Studio",
            "The canonical local-data directory is incorrect."))
    {
        return 1;
    }

    const UserDataPaths unavailable({}, {});
    if (!Expect(unavailable.ConfigurationDirectory().empty(),
            "A missing configuration root must stay unavailable.") ||
        !Expect(unavailable.LocalDataDirectory().empty(),
            "A missing local-data root must stay unavailable.") ||
        !Expect(!UserDataPaths::PathFromEnvironment({}),
            "An empty environment-variable name must not resolve to a path."))
    {
        return 2;
    }

    const UserDataPaths systemPaths = UserDataPaths::FromSystemEnvironment();
#if defined(_WIN32)
    const fs::path expectedConfiguration =
        UserDataPaths::PathFromEnvironment("APPDATA")
            .value_or(fs::path{}).lexically_normal();
    const fs::path expectedLocalData =
        UserDataPaths::PathFromEnvironment("LOCALAPPDATA")
            .value_or(fs::path{}).lexically_normal();
    if (!Expect(systemPaths.ConfigurationRoot() == expectedConfiguration,
            "APPDATA was not resolved as the configuration root.") ||
        !Expect(systemPaths.LocalDataRoot() == expectedLocalData,
            "LOCALAPPDATA was not resolved as the local-data root."))
    {
        return 3;
    }
#endif

    return 0;
}
