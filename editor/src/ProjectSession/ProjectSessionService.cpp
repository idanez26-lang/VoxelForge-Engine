#include "ProjectSession/ProjectSessionService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

namespace VoxelForge::Editor
{
namespace
{
std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.generic_u8string();
    return std::string(value.begin(), value.end());
}

std::filesystem::path FromUtf8(const std::string_view value)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

bool ParseUnsigned(const std::string_view text, std::uint32_t& value) noexcept
{
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool ParseFloat(const std::string_view text, float& value) noexcept
{
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = std::from_chars(
        begin, end, value, std::chars_format::general);
    return parsed.ec == std::errc{} && parsed.ptr == end &&
        std::isfinite(value);
}

bool ParseVector(
    const std::string_view text,
    ProjectSessionVector3& value) noexcept
{
    const std::size_t first = text.find(',');
    const std::size_t second = first == std::string_view::npos
        ? std::string_view::npos : text.find(',', first + 1U);
    if (first == std::string_view::npos ||
        second == std::string_view::npos ||
        text.find(',', second + 1U) != std::string_view::npos)
    {
        return false;
    }
    return ParseFloat(text.substr(0U, first), value.X) &&
        ParseFloat(text.substr(first + 1U, second - first - 1U), value.Y) &&
        ParseFloat(text.substr(second + 1U), value.Z);
}

std::optional<ProjectSessionCameraView> ParseView(
    const std::string_view value) noexcept
{
    if (value == "Perspective") return ProjectSessionCameraView::Perspective;
    if (value == "Front") return ProjectSessionCameraView::Front;
    if (value == "Back") return ProjectSessionCameraView::Back;
    if (value == "Left") return ProjectSessionCameraView::Left;
    if (value == "Right") return ProjectSessionCameraView::Right;
    if (value == "Top") return ProjectSessionCameraView::Top;
    if (value == "Bottom") return ProjectSessionCameraView::Bottom;
    return std::nullopt;
}

const char* ViewName(const ProjectSessionCameraView view) noexcept
{
    switch (view)
    {
    case ProjectSessionCameraView::Perspective: return "Perspective";
    case ProjectSessionCameraView::Front: return "Front";
    case ProjectSessionCameraView::Back: return "Back";
    case ProjectSessionCameraView::Left: return "Left";
    case ProjectSessionCameraView::Right: return "Right";
    case ProjectSessionCameraView::Top: return "Top";
    case ProjectSessionCameraView::Bottom: return "Bottom";
    }
    return "Perspective";
}

bool IsRegularFile(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
        !std::filesystem::is_symlink(status);
}

bool RemoveTransactionFile(
    const std::filesystem::path& path,
    std::string& error)
{
    if (!IsRegularFile(path))
    {
        std::error_code existsError;
        if (!std::filesystem::exists(path, existsError) && !existsError)
            return true;
        error = "Refusing to remove a non-regular session transaction file.";
        return false;
    }
    std::error_code filesystemError;
    if (std::filesystem::remove(path, filesystemError) && !filesystemError)
        return true;
    error = "Unable to remove session transaction file: " +
        filesystemError.message();
    return false;
}
}

bool ProjectSessionService::SetProjectRoot(
    const std::filesystem::path& projectRoot)
{
    ClearProject();
    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
        return false;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(canonical, error);
    if (error || std::filesystem::is_symlink(status)) return false;
    projectRoot_ = canonical;
    return true;
}

void ProjectSessionService::ClearProject() noexcept
{
    projectRoot_.clear();
}

ProjectSessionLoadResult ProjectSessionService::Load() const
{
    if (projectRoot_.empty())
        return {ProjectSessionLoadStatus::IoError, {}, false,
            "No project is configured for session restore."};

    const std::filesystem::path path = SessionPath();
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error) && !error)
            return {ProjectSessionLoadStatus::NotFound, {}, false, {}};
        return {ProjectSessionLoadStatus::IoError, {}, false,
            "Unable to read project session."};
    }

    std::map<std::string, std::string, std::less<>> values;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0U)
            return {ProjectSessionLoadStatus::Corrupt, {}, false,
                "Project session contains an invalid line."};
        std::string key = line.substr(0U, separator);
        std::string value = line.substr(separator + 1U);
        if (!values.emplace(std::move(key), std::move(value)).second)
            return {ProjectSessionLoadStatus::Corrupt, {}, false,
                "Project session contains a duplicate field."};
    }
    if (!input.eof())
        return {ProjectSessionLoadStatus::IoError, {}, false,
            "Unable to finish reading project session."};

    const auto format = values.find("format_version");
    if (format == values.end())
        return {ProjectSessionLoadStatus::Corrupt, {}, false,
            "Project session has no format version."};
    std::uint32_t version = 0U;
    if (!ParseUnsigned(format->second, version))
        return {ProjectSessionLoadStatus::Corrupt, {}, false,
            "Project session format version is invalid."};
    if (version != ProjectSessionData::FormatVersion)
        return {ProjectSessionLoadStatus::UnsupportedVersion, {}, false,
            "Project session format version is not supported."};

    constexpr std::array Required{
        "last_model", "camera_position", "camera_rotation",
        "camera_target", "camera_distance", "camera_view", "active_tool"};
    for (const std::string_view key : Required)
    {
        if (!values.contains(key))
            return {ProjectSessionLoadStatus::Corrupt, {}, false,
                "Project session is missing a required field."};
    }

    ProjectSessionData session;
    session.LastModel = FromUtf8(values.at("last_model"));
    if (!session.LastModel.empty() && !IsValidModelPath(session.LastModel))
        return {ProjectSessionLoadStatus::Corrupt, {}, false,
            "Project session model path is invalid."};

    const bool parsedCamera =
        ParseVector(values.at("camera_position"), session.Camera.Position) &&
        ParseVector(values.at("camera_rotation"),
            session.Camera.RotationDegrees) &&
        ParseVector(values.at("camera_target"), session.Camera.Target) &&
        ParseFloat(values.at("camera_distance"), session.Camera.Distance);
    const auto view = ParseView(values.at("camera_view"));
    if (view) session.Camera.View = *view;
    const bool cameraValid = parsedCamera && view && IsValidCamera(session.Camera);

    session.ActiveTool = values.at("active_tool") == "Eraser"
        ? ProjectSessionTool::Eraser : ProjectSessionTool::Pencil;
    return {ProjectSessionLoadStatus::Loaded, std::move(session), cameraValid,
        cameraValid ? std::string{} : "Project session camera is invalid."};
}

bool ProjectSessionService::Save(
    const ProjectSessionData& session,
    std::string& error) const
{
    error.clear();
    if (projectRoot_.empty())
    {
        error = "No project is configured for session save.";
        return false;
    }
    if (!session.LastModel.empty() && !IsValidModelPath(session.LastModel))
    {
        error = "Project session model path is invalid.";
        return false;
    }
    if (!IsValidCamera(session.Camera))
    {
        error = "Project session camera is invalid.";
        return false;
    }

    const std::filesystem::path destination = SessionPath();
    const std::filesystem::path temporary = destination.string() + ".tmp";
    const std::filesystem::path backup = destination.string() + ".bak";
    std::error_code filesystemError;
    for (const std::filesystem::path& transaction : {temporary, backup})
    {
        if (std::filesystem::exists(transaction, filesystemError) ||
            filesystemError)
        {
            error = "A project session transaction file already exists.";
            return false;
        }
    }

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Unable to create project session temporary file.";
        return false;
    }
    output << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "format_version=" << ProjectSessionData::FormatVersion << '\n'
        << "last_model=" << ToUtf8(session.LastModel) << '\n'
        << "camera_position=" << session.Camera.Position.X << ','
        << session.Camera.Position.Y << ',' << session.Camera.Position.Z << '\n'
        << "camera_rotation=" << session.Camera.RotationDegrees.X << ','
        << session.Camera.RotationDegrees.Y << ','
        << session.Camera.RotationDegrees.Z << '\n'
        << "camera_target=" << session.Camera.Target.X << ','
        << session.Camera.Target.Y << ',' << session.Camera.Target.Z << '\n'
        << "camera_distance=" << session.Camera.Distance << '\n'
        << "camera_view=" << ViewName(session.Camera.View) << '\n'
        << "active_tool="
        << (session.ActiveTool == ProjectSessionTool::Eraser
            ? "Eraser" : "Pencil") << '\n';
    output.flush();
    output.close();
    if (!output)
    {
        std::string ignored;
        static_cast<void>(RemoveTransactionFile(temporary, ignored));
        error = "Unable to write project session safely.";
        return false;
    }

    const bool hadDestination = IsRegularFile(destination);
    if (hadDestination)
    {
        std::filesystem::rename(destination, backup, filesystemError);
        if (filesystemError)
        {
            std::string ignored;
            static_cast<void>(RemoveTransactionFile(temporary, ignored));
            error = "Unable to back up the previous project session: " +
                filesystemError.message();
            return false;
        }
    }
    else if (std::filesystem::exists(destination, filesystemError) ||
        filesystemError)
    {
        std::string ignored;
        static_cast<void>(RemoveTransactionFile(temporary, ignored));
        error = "Project session path is not a regular file.";
        return false;
    }

    filesystemError.clear();
    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError)
    {
        if (hadDestination)
        {
            std::error_code rollbackError;
            std::filesystem::rename(backup, destination, rollbackError);
        }
        std::string ignored;
        static_cast<void>(RemoveTransactionFile(temporary, ignored));
        error = "Unable to replace project session: " +
            filesystemError.message();
        return false;
    }

    if (hadDestination && !RemoveTransactionFile(backup, error)) return false;
    return true;
}

const std::filesystem::path& ProjectSessionService::ProjectRoot() const noexcept
{
    return projectRoot_;
}

std::filesystem::path ProjectSessionService::SessionPath() const
{
    return projectRoot_.empty() ? std::filesystem::path{}
        : projectRoot_ / ".vfsession";
}

bool ProjectSessionService::IsValidCamera(
    const ProjectSessionCamera& camera) noexcept
{
    const auto finite = [](const ProjectSessionVector3& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) &&
            std::isfinite(value.Z);
    };
    return finite(camera.Position) && finite(camera.RotationDegrees) &&
        finite(camera.Target) && std::isfinite(camera.Distance) &&
        camera.Distance >= 0.1F && camera.Distance <= 10000.0F &&
        camera.RotationDegrees.X >= -89.0F &&
        camera.RotationDegrees.X <= 89.0F &&
        std::abs(camera.RotationDegrees.Z) <= 0.001F;
}

bool ProjectSessionService::IsValidModelPath(
    const std::filesystem::path& relativePath) noexcept
{
    if (relativePath.empty() || relativePath.is_absolute() ||
        relativePath.has_root_path())
        return false;
    const std::filesystem::path normalized = relativePath.lexically_normal();
    for (const auto& part : normalized)
        if (part == "..") return false;
    auto iterator = normalized.begin();
    if (iterator == normalized.end() || *iterator++ != "Assets") return false;
    if (iterator == normalized.end() || *iterator++ != "Models") return false;
    if (iterator == normalized.end()) return false;
    std::string extension = normalized.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return extension == ".vox";
}

} // namespace VoxelForge::Editor
