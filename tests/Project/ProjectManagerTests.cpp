#include "VoxelForge/Project/ProjectManager.h"
#include "VoxelForge/Project/RecentProjects.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto seed = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        const std::filesystem::path temporaryRoot =
            std::filesystem::temp_directory_path();

        for (int attempt = 0; attempt < 100; ++attempt)
        {
            path_ = temporaryRoot /
                ("VoxelForgeProjectTests-" + std::to_string(seed) + "-" +
                 std::to_string(attempt));
            std::error_code error;

            if (std::filesystem::create_directory(path_, error))
            {
                return;
            }
        }

        throw std::runtime_error(
            "Unable to create the project test directory.");
    }

    ~TemporaryDirectory()
    {
        std::error_code ignoredError;
        std::filesystem::remove_all(path_, ignoredError);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

int RunProjectManagerTests(const std::filesystem::path& temporaryRoot)
{
    namespace fs = std::filesystem;
    using VoxelForge::Project::ProjectManager;

    const fs::path projectsParent = temporaryRoot / "Projects";
    const fs::path recentProjectsFile =
        temporaryRoot / "Configuration" / "recent_projects.txt";
    fs::create_directory(projectsParent);

    ProjectManager manager(recentProjectsFile);

    if (manager.CreateProject("Bad:Name", projectsParent))
    {
        return 1;
    }

    const auto createdProject =
        manager.CreateProject("ArtisanProject", projectsParent);

    if (!createdProject)
    {
        return 2;
    }

    const fs::path expectedRoot = projectsParent / "ArtisanProject";
    const fs::path expectedProjectFile =
        expectedRoot / "ArtisanProject.vfproject";

    if (createdProject->Name() != "ArtisanProject" ||
        createdProject->RootPath() != expectedRoot ||
        createdProject->ProjectFilePath() != expectedProjectFile)
    {
        return 3;
    }

    if (!fs::is_regular_file(expectedProjectFile) ||
        !fs::is_directory(expectedRoot / "Assets") ||
        !fs::is_directory(expectedRoot / "Scenes") ||
        !fs::is_directory(expectedRoot / "Cache"))
    {
        return 4;
    }

    const std::string expectedMetadata =
        "# VoxelForge Project\n"
        "format_version=1\n"
        "name=ArtisanProject\n"
        "root=.\n";

    if (ReadTextFile(expectedProjectFile) != expectedMetadata)
    {
        return 5;
    }

    fs::path temporaryProjectFile = expectedProjectFile;
    temporaryProjectFile += ".tmp";
    fs::path backupProjectFile = expectedProjectFile;
    backupProjectFile += ".bak";

    {
        std::ofstream blockedSave(temporaryProjectFile);
        blockedSave << "unfinished save";
    }

    if (manager.SaveActiveProject() ||
        ReadTextFile(expectedProjectFile) != expectedMetadata)
    {
        return 16;
    }

    fs::remove(temporaryProjectFile);

    if (!manager.SaveActiveProject() ||
        fs::exists(temporaryProjectFile) ||
        fs::exists(backupProjectFile))
    {
        return 17;
    }

    manager.CloseProject();
    const auto openedProject = manager.OpenProject(expectedProjectFile);

    if (!openedProject || openedProject->Name() != "ArtisanProject" ||
        openedProject->RootPath() != expectedRoot ||
        openedProject->ProjectFilePath() != expectedProjectFile)
    {
        return 6;
    }

    if (!manager.SaveActiveProject())
    {
        return 7;
    }

    manager.CloseProject();

    {
        std::ofstream append(expectedProjectFile, std::ios::app);
        append << "future_field=ignored\n";
    }

    if (!manager.OpenProject(expectedProjectFile))
    {
        return 8;
    }

    if (manager.RecentProjectPaths().size() != 1)
    {
        return 9;
    }

    const fs::path invalidProjectFile =
        temporaryRoot / "Invalid.vfproject";

    {
        std::ofstream invalidProject(invalidProjectFile);
        invalidProject << "name=Invalid\nroot=.\n";
    }

    if (manager.OpenProject(invalidProjectFile))
    {
        return 10;
    }

    const fs::path occupiedRoot = projectsParent / "Occupied";
    fs::create_directory(occupiedRoot);

    {
        std::ofstream marker(occupiedRoot / "personal-file.txt");
        marker << "keep";
    }

    if (manager.CreateProject("Occupied", projectsParent) ||
        !fs::is_regular_file(occupiedRoot / "personal-file.txt"))
    {
        return 11;
    }

    for (int index = 0; index < 11; ++index)
    {
        if (!manager.CreateProject(
                "Recent" + std::to_string(index),
                projectsParent))
        {
            return 12;
        }
    }

    const auto& recentProjects = manager.RecentProjectPaths();

    if (recentProjects.size() != 10 ||
        recentProjects.front().stem() != "Recent10")
    {
        return 13;
    }

    const std::vector<fs::path> recentProjectsBeforeRemoval(
        recentProjects.begin(),
        recentProjects.end());
    const fs::path removedRecentProject = recentProjectsBeforeRemoval[4];

    if (!manager.RemoveRecentProject(removedRecentProject) ||
        !fs::is_regular_file(removedRecentProject))
    {
        return 20;
    }

    const auto& recentProjectsAfterRemoval = manager.RecentProjectPaths();

    if (recentProjectsAfterRemoval.size() != 9)
    {
        return 21;
    }

    std::size_t expectedIndex = 0;

    for (const fs::path& projectPath : recentProjectsBeforeRemoval)
    {
        if (projectPath == removedRecentProject)
        {
            continue;
        }

        if (recentProjectsAfterRemoval[expectedIndex] != projectPath)
        {
            return 22;
        }

        ++expectedIndex;
    }

    if (!manager.OpenProject(removedRecentProject) ||
        manager.RecentProjectPaths().size() != 10 ||
        manager.RecentProjectPaths().front() != removedRecentProject)
    {
        return 23;
    }

    ProjectManager reloadedManager(recentProjectsFile);
    const VoxelForge::Project::RecentProjects reloadedRecentProjects(
        recentProjectsFile);

    if (reloadedManager.RecentProjectPaths().size() != 10 ||
        reloadedManager.RecentProjectPaths().front() != removedRecentProject ||
        !reloadedRecentProjects.LastError().empty())
    {
        return 14;
    }

    fs::remove(reloadedManager.RecentProjectPaths().back());

    if (!reloadedManager.OpenProject(
            reloadedManager.RecentProjectPaths().front()))
    {
        return 18;
    }

    const VoxelForge::Project::RecentProjects prunedRecentProjects(
        recentProjectsFile);

    if (prunedRecentProjects.Projects().size() != 9)
    {
        return 19;
    }

    fs::remove(prunedRecentProjects.Projects().front());
    ProjectManager managerWithMissingEntry(recentProjectsFile);

    if (managerWithMissingEntry.RecentProjectPaths().size() != 8)
    {
        return 15;
    }

    return 0;
}

} // namespace

int main()
{
    try
    {
        const TemporaryDirectory temporaryDirectory;
        return RunProjectManagerTests(temporaryDirectory.Path());
    }
    catch (...)
    {
        return 100;
    }
}
