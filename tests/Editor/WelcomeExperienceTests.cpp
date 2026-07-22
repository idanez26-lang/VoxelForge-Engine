#include "Welcome/ProjectDeletionService.h"
#include "Welcome/WelcomeScreenModel.h"

#include "VoxelForge/Project/ProjectManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge;
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class Fixture final
{
public:
    Fixture()
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        Root = fs::temp_directory_path() /
            ("VoxelForgeWelcome-" + std::to_string(unique));
        fs::create_directories(Root);
    }
    ~Fixture()
    {
        std::error_code ignored;
        fs::remove_all(Root, ignored);
    }
    fs::path Root;
};

class FakeRecycleBin final : public IProjectRecycleBin
{
public:
    bool MoveDirectory(const fs::path& directory, std::string& error) override
    {
        ++CallCount;
        LastDirectory = directory;
        if (!Succeed)
        {
            error = "Controlled Recycle Bin failure.";
            return false;
        }
        if (MovePhysically)
        {
            Quarantine = directory.parent_path() /
                (directory.filename().string() + ".recycled");
            std::error_code moveError;
            fs::rename(directory, Quarantine, moveError);
            if (moveError)
            {
                error = moveError.message();
                return false;
            }
        }
        return true;
    }

    bool Succeed = true;
    bool MovePhysically = false;
    std::size_t CallCount = 0U;
    fs::path LastDirectory;
    fs::path Quarantine;
};

std::shared_ptr<Project::Project> CreateProject(
    Project::ProjectManager& manager,
    const fs::path& parent,
    const std::string& name)
{
    const auto project = manager.CreateProject(name, parent);
    Require(project != nullptr, "Unable to create a controlled test project.");
    return project;
}

void TestWelcomeModel()
{
    Fixture fixture;
    const fs::path available = fixture.Root / "Available.vfproject";
    std::ofstream(available) << "placeholder";
    const fs::path missing = fixture.Root / "Missing.vfproject";
    const std::vector<fs::path> paths{available, missing};
    const auto entries = WelcomeScreenModel::BuildEntries(paths);
    Require(entries.size() == 2U && entries[0].CanOpen() &&
            entries[0].CanDelete() && !entries[1].CanOpen() &&
            !entries[1].CanDelete(),
        "Welcome recent-project availability is incorrect.");
    Require(!WelcomeScreenModel::ShowEditorPanels(false) &&
            WelcomeScreenModel::ShowEditorPanels(true),
        "Welcome/editor panel state transition is incorrect.");
    for (const auto [width, height] : {
             std::pair{480.0F, 640.0F},
             std::pair{1920.0F, 1080.0F},
             std::pair{3840.0F, 2160.0F}})
    {
        const WelcomeScreenLayout layout =
            WelcomeScreenModel::CalculateLayout(width, height);
        Require(layout.ContentWidth > 0.0F &&
                layout.ContentWidth <= width && layout.TopPadding >= 0.0F &&
                layout.CardHeight >= 112.0F,
            "Welcome layout is invalid for a supported viewport size.");
    }
}

void TestDeletionValidationAndRecycleFailure()
{
    Fixture fixture;
    const fs::path recentFile = fixture.Root / "recent.txt";
    Project::ProjectManager manager(recentFile);
    const auto project = CreateProject(manager, fixture.Root, "DeleteMe");
    const fs::path projectFile = project->ProjectFilePath();
    const fs::path projectRoot = project->RootPath();
    manager.CloseProject();

    FakeRecycleBin backend;
    ProjectDeletionService service(backend);
    ProjectDeletionRequest request{projectFile, std::nullopt, {}, false};
    Require(service.DeleteProject(request).Status ==
                ProjectDeletionStatus::ConfirmationRequired &&
            backend.CallCount == 0U,
        "Deletion ran without explicit confirmation.");

    request.Confirmed = true;
    request.ActiveProjectRoot = projectRoot;
    Require(service.DeleteProject(request).Status ==
                ProjectDeletionStatus::ActiveProject &&
            backend.CallCount == 0U,
        "The active project was not protected.");
    request.ActiveProjectRoot.reset();
    request.ProtectedRoots = {projectRoot};
    Require(service.DeleteProject(request).Status ==
                ProjectDeletionStatus::DangerousPath,
        "A protected project root was accepted.");
    request.ProtectedRoots.clear();

    backend.Succeed = false;
    const ProjectDeletionResult failed = service.DeleteProject(request);
    Require(failed.Status == ProjectDeletionStatus::RecycleBinFailed &&
            fs::is_directory(projectRoot) && fs::is_regular_file(projectFile),
        "Recycle Bin failure did not preserve the project.");

    backend.Succeed = true;
    const ProjectDeletionResult succeeded = service.DeleteProject(request);
    Require(succeeded.Succeeded() && backend.LastDirectory == projectRoot &&
            fs::is_directory(projectRoot),
        "Valid deletion did not target exactly the validated project root.");
}

void TestDangerousAndInvalidPaths()
{
    Fixture fixture;
    FakeRecycleBin backend;
    ProjectDeletionService service(backend);
    Require(service.DeleteProject({{}, std::nullopt, {}, true}).Status ==
                ProjectDeletionStatus::InvalidPath,
        "An empty path was accepted.");
    Require(service.DeleteProject({"relative.vfproject", std::nullopt, {}, true})
                .Status == ProjectDeletionStatus::InvalidPath,
        "A relative path was accepted.");
    Require(service.DeleteProject(
                {fixture.Root.root_path(), std::nullopt, {}, true}).Status !=
                ProjectDeletionStatus::Success,
        "A drive root was accepted.");

    const fs::path noProject = fixture.Root / "NoProject";
    fs::create_directory(noProject);
    Require(service.DeleteProject(
                {noProject / "Missing.vfproject", std::nullopt, {}, true})
                .Status == ProjectDeletionStatus::ProjectNotFound,
        "A folder without a project file was accepted.");

    const fs::path malformedRoot = fixture.Root / "Malformed";
    fs::create_directory(malformedRoot);
    const fs::path malformed = malformedRoot / "Malformed.vfproject";
    std::ofstream(malformed) << "not-a-project";
    Require(service.DeleteProject(
                {malformed, std::nullopt, {}, true}).Status ==
                ProjectDeletionStatus::InvalidProject,
        "A malformed project was accepted.");
    Require(backend.CallCount == 0U,
        "An invalid path reached the Recycle Bin backend.");
}

void TestIdentityAndSymbolicLinks()
{
    Fixture fixture;
    const fs::path recentFile = fixture.Root / "recent.txt";
    Project::ProjectManager manager(recentFile);
    const auto project = CreateProject(manager, fixture.Root, "Identity");
    const fs::path projectFile = project->ProjectFilePath();
    const fs::path projectRoot = project->RootPath();
    manager.CloseProject();

    FakeRecycleBin backend;
    ProjectDeletionService service(backend);
    const fs::path renamedProjectFile = projectRoot / "Other.vfproject";
    fs::copy_file(projectFile, renamedProjectFile);
    Require(service.DeleteProject(
                {renamedProjectFile, std::nullopt, {}, true}).Status ==
                ProjectDeletionStatus::IdentityMismatch &&
            backend.CallCount == 0U,
        "A project file with a mismatched identity was accepted.");

    const fs::path external = fixture.Root / "External";
    fs::create_directory(external);
    const fs::path link = projectRoot / "ExternalLink";
    std::error_code linkError;
    fs::create_directory_symlink(external, link, linkError);
    if (!linkError)
    {
        Require(service.DeleteProject(
                    {projectFile, std::nullopt, {}, true}).Status ==
                    ProjectDeletionStatus::SymbolicLink &&
                backend.CallCount == 0U,
            "A project containing an external symbolic link was accepted.");
    }
}

void TestRecentListSemantics()
{
    Fixture fixture;
    const fs::path recentFile = fixture.Root / "recent.txt";
    Project::ProjectManager manager(recentFile);
    const auto first = CreateProject(manager, fixture.Root, "KeepMe");
    const fs::path firstFile = first->ProjectFilePath();
    const auto second = CreateProject(manager, fixture.Root, "RecycleMe");
    const fs::path secondFile = second->ProjectFilePath();
    manager.CloseProject();

    Require(manager.RemoveRecentProject(firstFile) &&
            fs::is_regular_file(firstFile),
        "Remove from recent list touched project files.");

    FakeRecycleBin backend;
    backend.MovePhysically = true;
    ProjectDeletionService service(backend);
    const ProjectDeletionResult result = service.DeleteProject(
        {secondFile, std::nullopt, {}, true});
    Require(result.Succeeded() && !fs::exists(secondFile) &&
            fs::is_directory(backend.Quarantine) &&
            manager.RemoveRecentProject(secondFile) &&
            manager.RecentProjectPaths().empty() &&
            fs::is_regular_file(firstFile),
        "Successful deletion did not preserve unrelated projects or recents.");
}

void TestRecycleFailurePreservesRecentEntry()
{
    Fixture fixture;
    Project::ProjectManager manager(fixture.Root / "recent.txt");
    const auto project = CreateProject(manager, fixture.Root, "StillRecent");
    const fs::path projectFile = project->ProjectFilePath();
    manager.CloseProject();

    FakeRecycleBin backend;
    backend.Succeed = false;
    ProjectDeletionService service(backend);
    const ProjectDeletionResult result = service.DeleteProject(
        {projectFile, std::nullopt, {}, true});
    Require(!result.Succeeded() && fs::is_regular_file(projectFile) &&
            manager.RecentProjectPaths().size() == 1U &&
            manager.RecentProjectPaths().front() == projectFile,
        "A failed Recycle Bin operation changed the recent-project entry.");
}
}

int main()
{
    try
    {
        TestWelcomeModel();
        TestDeletionValidationAndRecycleFailure();
        TestDangerousAndInvalidPaths();
        TestIdentityAndSymbolicLinks();
        TestRecentListSemantics();
        TestRecycleFailurePreservesRecentEntry();
        std::cout << "Welcome experience and safe deletion tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Welcome experience tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
