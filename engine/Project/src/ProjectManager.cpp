#include "VoxelForge/Project/ProjectManager.h"

#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Project/ProjectSerializer.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string_view>
#include <utility>

namespace VoxelForge::Project
{

namespace
{

constexpr std::string_view ProjectExtension = ".vfproject";

std::string ToUpper(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::toupper(character));
        });
    return value;
}

bool HasProjectExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return extension == ProjectExtension;
}

bool ValidateProjectName(const std::string& name, std::string& error)
{
    if (name.empty())
    {
        error = "Project name cannot be empty.";
        return false;
    }

    if (name == "." || name == "..")
    {
        error = "Project name cannot be . or ...";
        return false;
    }

    constexpr std::string_view ForbiddenCharacters = "<>:\"/\\|?*";

    for (const unsigned char character : name)
    {
        if (character < 32 ||
            ForbiddenCharacters.find(static_cast<char>(character)) !=
                std::string_view::npos)
        {
            error = "Project name contains a character forbidden by Windows.";
            return false;
        }
    }

    if (name.back() == ' ' || name.back() == '.')
    {
        error = "Project name cannot end with a space or a period.";
        return false;
    }

    const std::size_t period = name.find('.');
    const std::string baseName = ToUpper(name.substr(0, period));
    constexpr std::string_view ReservedNames[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
        "LPT6", "LPT7", "LPT8", "LPT9"};

    if (std::any_of(
            std::begin(ReservedNames),
            std::end(ReservedNames),
            [&baseName](const std::string_view reserved)
            {
                return baseName == reserved;
            }))
    {
        error = "Project name is reserved by Windows.";
        return false;
    }

    return true;
}

void CleanupCreatedScaffold(
    const std::filesystem::path& rootPath,
    const std::filesystem::path& projectFilePath,
    const bool rootWasCreated) noexcept
{
    std::error_code ignoredError;
    std::filesystem::remove(projectFilePath, ignoredError);
    ignoredError.clear();
    std::filesystem::remove(rootPath / "Cache", ignoredError);
    ignoredError.clear();
    std::filesystem::remove(rootPath / "Scenes", ignoredError);
    ignoredError.clear();
    std::filesystem::remove(rootPath / "Assets", ignoredError);

    if (rootWasCreated)
    {
        ignoredError.clear();
        std::filesystem::remove(rootPath, ignoredError);
    }
}

bool HasRequiredProjectDirectories(const std::filesystem::path& rootPath)
{
    std::error_code error;

    for (const std::string_view directory : {"Assets", "Scenes", "Cache"})
    {
        if (!std::filesystem::is_directory(rootPath / directory, error) || error)
        {
            return false;
        }
    }

    return true;
}

} // namespace

ProjectManager::ProjectManager(std::filesystem::path recentProjectsFilePath)
    : recentProjects_(std::move(recentProjectsFilePath))
{
    if (!recentProjects_.LastError().empty())
    {
        Core::Logger::Instance().Warning(recentProjects_.LastError());
    }
}

std::shared_ptr<Project> ProjectManager::CreateProject(
    std::string name,
    const std::filesystem::path& parentDirectory)
{
    lastError_.clear();

    if (!ValidateProjectName(name, lastError_))
    {
        return {};
    }

    if (parentDirectory.empty())
    {
        SetError("Project parent directory cannot be empty.");
        return {};
    }

    std::error_code filesystemError;
    std::filesystem::path absoluteParent = std::filesystem::absolute(
        parentDirectory,
        filesystemError);

    if (filesystemError ||
        !std::filesystem::is_directory(absoluteParent, filesystemError) ||
        filesystemError)
    {
        SetError("Project parent directory does not exist or is not accessible.");
        return {};
    }

    absoluteParent = absoluteParent.lexically_normal();
    const std::filesystem::path rootPath = absoluteParent / name;
    const std::filesystem::path projectFilePath =
        rootPath / (name + std::string(ProjectExtension));
    bool rootWasCreated = false;

    const bool rootExists = std::filesystem::exists(
        rootPath,
        filesystemError);

    if (filesystemError)
    {
        SetError("Unable to inspect the destination project directory: " +
            filesystemError.message());
        return {};
    }

    if (rootExists)
    {
        if (!std::filesystem::is_directory(rootPath, filesystemError) ||
            filesystemError)
        {
            SetError("The project destination exists and is not a directory.");
            return {};
        }

        const bool rootIsEmpty = std::filesystem::is_empty(
            rootPath,
            filesystemError);

        if (filesystemError || !rootIsEmpty)
        {
            SetError("The project destination directory is not empty.");
            return {};
        }
    }
    else
    {
        if (!std::filesystem::create_directory(rootPath, filesystemError) ||
            filesystemError)
        {
            SetError("Unable to create the project directory: " +
                filesystemError.message());
            return {};
        }

        rootWasCreated = true;
    }

    for (const std::string_view directory : {"Assets", "Scenes", "Cache"})
    {
        filesystemError.clear();

        if (!std::filesystem::create_directory(
                rootPath / directory,
                filesystemError) || filesystemError)
        {
            SetError("Unable to create the project directory " +
                std::string(directory) + ": " + filesystemError.message());
            CleanupCreatedScaffold(rootPath, projectFilePath, rootWasCreated);
            return {};
        }
    }

    auto project = std::make_shared<Project>(
        std::move(name),
        rootPath,
        projectFilePath);

    if (!ProjectSerializer::Save(*project, lastError_))
    {
        CleanupCreatedScaffold(rootPath, projectFilePath, rootWasCreated);
        Core::Logger::Instance().Error(lastError_);
        return {};
    }

    activeProject_ = project;
    RecordRecentProject(projectFilePath);
    Core::Logger::Instance().Info("Project created: " + project->Name());
    return activeProject_;
}

std::shared_ptr<Project> ProjectManager::OpenProject(
    const std::filesystem::path& projectFilePath)
{
    lastError_.clear();

    if (projectFilePath.empty())
    {
        SetError("Project file path cannot be empty.");
        return {};
    }

    std::error_code filesystemError;
    std::filesystem::path absoluteProjectFile = std::filesystem::absolute(
        projectFilePath,
        filesystemError);

    if (filesystemError)
    {
        SetError("Unable to resolve the project file path: " +
            filesystemError.message());
        return {};
    }

    absoluteProjectFile = absoluteProjectFile.lexically_normal();

    if (!HasProjectExtension(absoluteProjectFile))
    {
        SetError("Project file must use the .vfproject extension.");
        return {};
    }

    if (!std::filesystem::is_regular_file(
            absoluteProjectFile,
            filesystemError) || filesystemError)
    {
        SetError("Project file does not exist or is not accessible.");
        return {};
    }

    const auto projectData = ProjectSerializer::Load(
        absoluteProjectFile,
        lastError_);

    if (!projectData)
    {
        Core::Logger::Instance().Error(lastError_);
        return {};
    }

    if (!ValidateProjectName(projectData->Name, lastError_))
    {
        Core::Logger::Instance().Error(lastError_);
        return {};
    }

    const std::filesystem::path expectedFileName =
        projectData->Name + std::string(ProjectExtension);

    if (absoluteProjectFile.filename() != expectedFileName)
    {
        SetError("Project file name must match the project name.");
        return {};
    }

    if (!HasRequiredProjectDirectories(projectData->RootPath))
    {
        SetError("Project is missing Assets, Scenes, or Cache directory.");
        return {};
    }

    activeProject_ = std::make_shared<Project>(
        projectData->Name,
        projectData->RootPath,
        absoluteProjectFile);
    RecordRecentProject(absoluteProjectFile);
    Core::Logger::Instance().Info(
        "Project opened: " + activeProject_->Name());
    return activeProject_;
}

bool ProjectManager::SaveActiveProject()
{
    lastError_.clear();

    if (!activeProject_)
    {
        SetError("No project is currently open.");
        return false;
    }

    if (!ProjectSerializer::Save(*activeProject_, lastError_))
    {
        Core::Logger::Instance().Error(lastError_);
        return false;
    }

    Core::Logger::Instance().Info(
        "Project saved: " + activeProject_->Name());
    return true;
}

void ProjectManager::CloseProject() noexcept
{
    if (activeProject_)
    {
        Core::Logger::Instance().Info(
            "Project closed: " + activeProject_->Name());
    }

    activeProject_.reset();
    lastError_.clear();
}

bool ProjectManager::HasActiveProject() const noexcept
{
    return activeProject_ != nullptr;
}

const std::shared_ptr<Project>& ProjectManager::ActiveProject() const noexcept
{
    return activeProject_;
}

const std::vector<std::filesystem::path>& ProjectManager::RecentProjectPaths() const noexcept
{
    return recentProjects_.Projects();
}

const std::filesystem::path& ProjectManager::RecentProjectsFilePath() const noexcept
{
    return recentProjects_.StorageFilePath();
}

const std::string& ProjectManager::LastError() const noexcept
{
    return lastError_;
}

void ProjectManager::RecordRecentProject(
    const std::filesystem::path& projectFilePath)
{
    if (!recentProjects_.Add(projectFilePath))
    {
        Core::Logger::Instance().Warning(recentProjects_.LastError());
    }
}

void ProjectManager::SetError(std::string error)
{
    lastError_ = std::move(error);
    Core::Logger::Instance().Error(lastError_);
}

} // namespace VoxelForge::Project
