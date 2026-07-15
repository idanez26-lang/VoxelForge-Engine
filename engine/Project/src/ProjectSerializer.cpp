#include "VoxelForge/Project/ProjectSerializer.h"

#include <charconv>
#include <fstream>
#include <string_view>
#include <utility>

namespace VoxelForge::Project
{

namespace
{

bool ParseFormatVersion(
    const std::string_view text,
    std::uint32_t& version)
{
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, version);
    return result.ec == std::errc{} && result.ptr == end;
}

std::filesystem::path AuxiliaryPath(
    std::filesystem::path projectFilePath,
    const std::string_view suffix)
{
    projectFilePath += suffix;
    return projectFilePath;
}

void RemoveTemporaryFile(const std::filesystem::path& path) noexcept
{
    std::error_code ignoredError;
    std::filesystem::remove(path, ignoredError);
}

bool ReplaceProjectFile(
    const std::filesystem::path& temporaryFilePath,
    const std::filesystem::path& projectFilePath,
    const std::filesystem::path& backupFilePath,
    std::string& error)
{
    std::error_code filesystemError;
    const bool projectFileExists = std::filesystem::exists(
        projectFilePath,
        filesystemError);

    if (filesystemError)
    {
        error = "Unable to inspect the existing project file: " +
            filesystemError.message();
        RemoveTemporaryFile(temporaryFilePath);
        return false;
    }

    if (!projectFileExists)
    {
        std::filesystem::rename(
            temporaryFilePath,
            projectFilePath,
            filesystemError);

        if (filesystemError)
        {
            error = "Unable to install the new project file: " +
                filesystemError.message();
            RemoveTemporaryFile(temporaryFilePath);
            return false;
        }

        return true;
    }

    std::filesystem::rename(
        projectFilePath,
        backupFilePath,
        filesystemError);

    if (filesystemError)
    {
        error = "Unable to protect the existing project file: " +
            filesystemError.message();
        RemoveTemporaryFile(temporaryFilePath);
        return false;
    }

    filesystemError.clear();
    std::filesystem::rename(
        temporaryFilePath,
        projectFilePath,
        filesystemError);

    if (filesystemError)
    {
        const std::string replacementError = filesystemError.message();
        std::error_code rollbackError;
        std::filesystem::rename(
            backupFilePath,
            projectFilePath,
            rollbackError);
        RemoveTemporaryFile(temporaryFilePath);

        error = "Unable to replace the project file: " + replacementError;

        if (rollbackError)
        {
            error += ". The previous file remains available at: " +
                backupFilePath.string();
        }

        return false;
    }

    filesystemError.clear();
    std::filesystem::remove(backupFilePath, filesystemError);

    if (filesystemError)
    {
        error = "Project saved, but its backup could not be removed: " +
            filesystemError.message();
        return false;
    }

    return true;
}

} // namespace

bool ProjectSerializer::Save(
    const Project& project,
    std::string& error)
{
    error.clear();
    const std::filesystem::path temporaryFilePath = AuxiliaryPath(
        project.ProjectFilePath(),
        ".tmp");
    const std::filesystem::path backupFilePath = AuxiliaryPath(
        project.ProjectFilePath(),
        ".bak");
    std::error_code filesystemError;

    for (const std::filesystem::path& auxiliaryPath :
         {temporaryFilePath, backupFilePath})
    {
        const bool auxiliaryPathExists = std::filesystem::exists(
            auxiliaryPath,
            filesystemError);

        if (filesystemError)
        {
            error = "Unable to inspect the project save files: " +
                filesystemError.message();
            return false;
        }

        if (auxiliaryPathExists)
        {
            error = "A previous project save requires attention: " +
                auxiliaryPath.string();
            return false;
        }
    }

    std::ofstream output(
        temporaryFilePath,
        std::ios::binary | std::ios::trunc);

    if (!output)
    {
        error = "Unable to open the temporary project file for writing: " +
            temporaryFilePath.string();
        return false;
    }

    output << "# VoxelForge Project\n"
           << "format_version=" << FormatVersion << '\n'
           << "name=" << project.Name() << '\n'
           << "root=.\n";

    output.flush();

    if (!output)
    {
        error = "Unable to write the project metadata: " +
            temporaryFilePath.string();
        output.close();
        RemoveTemporaryFile(temporaryFilePath);
        return false;
    }

    output.close();

    if (!output)
    {
        error = "Unable to close the temporary project file: " +
            temporaryFilePath.string();
        RemoveTemporaryFile(temporaryFilePath);
        return false;
    }

    return ReplaceProjectFile(
        temporaryFilePath,
        project.ProjectFilePath(),
        backupFilePath,
        error);
}

std::optional<ProjectFileData> ProjectSerializer::Load(
    const std::filesystem::path& projectFilePath,
    std::string& error)
{
    error.clear();

    std::ifstream input(projectFilePath, std::ios::binary);

    if (!input)
    {
        error = "Unable to open the project file: " +
            projectFilePath.string();
        return std::nullopt;
    }

    bool hasFormatVersion = false;
    bool hasName = false;
    bool hasRoot = false;
    std::uint32_t formatVersion = 0;
    std::string name;
    std::string root;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(input, line))
    {
        ++lineNumber;

        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        const std::size_t separator = line.find('=');

        if (separator == std::string::npos)
        {
            error = "Invalid project file line " +
                std::to_string(lineNumber) + ": expected key=value.";
            return std::nullopt;
        }

        const std::string_view key(line.data(), separator);
        const std::string_view value(
            line.data() + separator + 1,
            line.size() - separator - 1);

        if (key == "format_version")
        {
            if (hasFormatVersion || !ParseFormatVersion(value, formatVersion))
            {
                error = "Invalid or duplicate format_version field.";
                return std::nullopt;
            }

            hasFormatVersion = true;
        }
        else if (key == "name")
        {
            if (hasName || value.empty())
            {
                error = "Invalid or duplicate project name field.";
                return std::nullopt;
            }

            name.assign(value);
            hasName = true;
        }
        else if (key == "root")
        {
            if (hasRoot || value.empty())
            {
                error = "Invalid or duplicate project root field.";
                return std::nullopt;
            }

            root.assign(value);
            hasRoot = true;
        }
    }

    if (!input.eof())
    {
        error = "Unable to read the complete project file.";
        return std::nullopt;
    }

    if (!hasFormatVersion || !hasName || !hasRoot)
    {
        error = "Project file is missing format_version, name, or root.";
        return std::nullopt;
    }

    if (formatVersion != FormatVersion)
    {
        error = "Unsupported project format version: " +
            std::to_string(formatVersion) + ".";
        return std::nullopt;
    }

    if (root != ".")
    {
        error = "Project format version 1 requires root=.";
        return std::nullopt;
    }

    std::error_code filesystemError;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(
        projectFilePath.parent_path(),
        filesystemError);

    if (filesystemError)
    {
        error = "Unable to resolve the project root path: " +
            filesystemError.message();
        return std::nullopt;
    }

    return ProjectFileData{
        std::move(name),
        absoluteRoot.lexically_normal()};
}

} // namespace VoxelForge::Project
