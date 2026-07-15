#include "VoxelForge/Project/RecentProjects.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace VoxelForge::Project
{

namespace
{

std::optional<std::string> ReadEnvironmentVariable(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;

    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }

    const std::unique_ptr<char, decltype(&std::free)> ownedValue(
        value,
        &std::free);

    if (length <= 1)
    {
        return std::nullopt;
    }

    return std::string(ownedValue.get());
#else
    const char* const value = std::getenv(name);

    if (value == nullptr || *value == '\0')
    {
        return std::nullopt;
    }

    return std::string(value);
#endif
}

std::string PathKey(const std::filesystem::path& path)
{
    std::string key = path.lexically_normal().generic_string();

#if defined(_WIN32)
    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
#endif

    return key;
}

bool IsProjectFile(const std::filesystem::path& path)
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
    return extension == ".vfproject";
}

std::optional<std::filesystem::path> ResolveExistingProjectFile(
    const std::filesystem::path& path)
{
    std::error_code error;

    if (!std::filesystem::is_regular_file(path, error) || error ||
        !IsProjectFile(path))
    {
        return std::nullopt;
    }

    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(path, error);

    if (error)
    {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
    }

    if (error)
    {
        return std::nullopt;
    }

    return normalized.lexically_normal();
}

} // namespace

RecentProjects::RecentProjects(std::filesystem::path storageFilePath)
    : storageFilePath_(std::move(storageFilePath))
{
    static_cast<void>(Load());
}

bool RecentProjects::Load()
{
    projects_.clear();
    lastError_.clear();

    if (storageFilePath_.empty())
    {
        return true;
    }

    std::error_code error;

    if (!std::filesystem::exists(storageFilePath_, error))
    {
        if (error)
        {
            lastError_ = "Unable to inspect the recent projects file: " +
                error.message();
            return false;
        }

        return true;
    }

    std::ifstream input(storageFilePath_, std::ios::binary);

    if (!input)
    {
        lastError_ = "Unable to open the recent projects file: " +
            storageFilePath_.string();
        return false;
    }

    std::string line;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            continue;
        }

        if (projects_.size() >= MaximumProjectCount)
        {
            continue;
        }

        const auto resolved = ResolveExistingProjectFile(line);

        if (!resolved)
        {
            continue;
        }

        const std::string candidateKey = PathKey(*resolved);
        const bool duplicate = std::any_of(
            projects_.begin(),
            projects_.end(),
            [&candidateKey](const std::filesystem::path& existing)
            {
                return PathKey(existing) == candidateKey;
            });

        if (!duplicate)
        {
            projects_.push_back(*resolved);
        }
    }

    if (!input.eof())
    {
        lastError_ = "Unable to read the complete recent projects file.";
        return false;
    }

    return true;
}

bool RecentProjects::Add(const std::filesystem::path& projectFilePath)
{
    lastError_.clear();
    const auto resolved = ResolveExistingProjectFile(projectFilePath);

    if (!resolved)
    {
        lastError_ = "Recent project does not exist or is not a .vfproject file: " +
            projectFilePath.string();
        return false;
    }

    const std::string projectKey = PathKey(*resolved);
    std::erase_if(
        projects_,
        [&projectKey](const std::filesystem::path& existing)
        {
            return PathKey(existing) == projectKey;
        });

    projects_.insert(projects_.begin(), *resolved);

    if (projects_.size() > MaximumProjectCount)
    {
        projects_.resize(MaximumProjectCount);
    }

    return Save();
}

const std::vector<std::filesystem::path>& RecentProjects::Projects() const noexcept
{
    return projects_;
}

const std::filesystem::path& RecentProjects::StorageFilePath() const noexcept
{
    return storageFilePath_;
}

const std::string& RecentProjects::LastError() const noexcept
{
    return lastError_;
}

std::filesystem::path RecentProjects::DefaultStorageFilePath()
{
    if (const auto overridePath =
            ReadEnvironmentVariable("VOXELFORGE_RECENT_PROJECTS_FILE"))
    {
        return std::filesystem::path(*overridePath);
    }

#if defined(_WIN32)
    if (const auto applicationData = ReadEnvironmentVariable("APPDATA"))
    {
        return std::filesystem::path(*applicationData) /
            "VoxelForgeStudio" / "recent_projects.txt";
    }
#else
    if (const auto configurationHome =
            ReadEnvironmentVariable("XDG_CONFIG_HOME"))
    {
        return std::filesystem::path(*configurationHome) /
            "VoxelForgeStudio" / "recent_projects.txt";
    }

    if (const auto home = ReadEnvironmentVariable("HOME"))
    {
        return std::filesystem::path(*home) / ".config" /
            "VoxelForgeStudio" / "recent_projects.txt";
    }
#endif

    return {};
}

bool RecentProjects::Save()
{
    if (storageFilePath_.empty())
    {
        return true;
    }

    std::erase_if(
        projects_,
        [](const std::filesystem::path& project)
        {
            return !ResolveExistingProjectFile(project).has_value();
        });

    std::error_code error;
    const std::filesystem::path parent = storageFilePath_.parent_path();

    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);

        if (error)
        {
            lastError_ = "Unable to create the recent projects directory: " +
                error.message();
            return false;
        }
    }

    std::ofstream output(
        storageFilePath_,
        std::ios::binary | std::ios::trunc);

    if (!output)
    {
        lastError_ = "Unable to write the recent projects file: " +
            storageFilePath_.string();
        return false;
    }

    for (const std::filesystem::path& project : projects_)
    {
        output << project.generic_string() << '\n';
    }

    if (!output)
    {
        lastError_ = "Unable to write all recent project entries.";
        return false;
    }

    return true;
}

} // namespace VoxelForge::Project
