#include "Project/ProjectDialogPreferences.h"

#include <cstdlib>
#include <fstream>
#include <optional>

namespace VoxelForge::Editor
{
namespace
{
std::filesystem::path FromUtf8(const std::string_view value)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

std::string ToUtf8(const std::filesystem::path& value)
{
    const std::u8string utf8 = value.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::optional<std::string> Environment(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result.empty() ? std::nullopt : std::optional<std::string>(result);
#else
    const char* value = std::getenv(name);
    return value == nullptr || *value == '\0'
        ? std::nullopt : std::optional<std::string>(value);
#endif
}
}

ProjectDialogPreferences::ProjectDialogPreferences(
    std::filesystem::path storageFilePath)
    : storageFilePath_(std::move(storageFilePath))
{
}

bool ProjectDialogPreferences::Load()
{
    lastError_.clear();
    lastCreateParent_.clear();
    lastOpenDirectory_.clear();
    firstCreationCompleted_ = false;
    if (storageFilePath_.empty()) return true;

    std::ifstream input(storageFilePath_);
    if (!input)
    {
        std::error_code error;
        if (!std::filesystem::exists(storageFilePath_, error) && !error)
            return true;
        lastError_ = "Unable to read project dialog preferences.";
        return false;
    }

    std::string line;
    while (std::getline(input, line))
    {
        constexpr std::string_view CreateKey = "last_create_parent=";
        constexpr std::string_view OpenKey = "last_open_directory=";
        constexpr std::string_view FirstCreationKey =
            "first_creation_completed=";
        if (line.starts_with(CreateKey))
            lastCreateParent_ = ExistingDirectoryOrEmpty(
                FromUtf8(line.substr(CreateKey.size())));
        else if (line.starts_with(OpenKey))
            lastOpenDirectory_ = ExistingDirectoryOrEmpty(
                FromUtf8(line.substr(OpenKey.size())));
        else if (line.starts_with(FirstCreationKey))
            firstCreationCompleted_ =
                line.substr(FirstCreationKey.size()) == "1";
    }
    return true;
}

bool ProjectDialogPreferences::SetLastCreateParent(std::filesystem::path path)
{
    lastCreateParent_ = ExistingDirectoryOrEmpty(path);
    return Save();
}

bool ProjectDialogPreferences::SetLastOpenDirectory(std::filesystem::path path)
{
    lastOpenDirectory_ = ExistingDirectoryOrEmpty(path);
    return Save();
}

bool ProjectDialogPreferences::SetFirstCreationCompleted(const bool completed)
{
    firstCreationCompleted_ = completed;
    return Save();
}

const std::filesystem::path& ProjectDialogPreferences::LastCreateParent() const noexcept
{
    return lastCreateParent_;
}

const std::filesystem::path& ProjectDialogPreferences::LastOpenDirectory() const noexcept
{
    return lastOpenDirectory_;
}

bool ProjectDialogPreferences::FirstCreationCompleted() const noexcept
{
    return firstCreationCompleted_;
}

const std::filesystem::path& ProjectDialogPreferences::StorageFilePath() const noexcept
{
    return storageFilePath_;
}

const std::string& ProjectDialogPreferences::LastError() const noexcept
{
    return lastError_;
}

std::filesystem::path ProjectDialogPreferences::DefaultStorageFilePath()
{
    if (const auto overridePath = Environment("VOXELFORGE_PREFERENCES_FILE"))
        return std::filesystem::path(*overridePath);
#if defined(_WIN32)
    if (const auto appData = Environment("APPDATA"))
        return std::filesystem::path(*appData) / "VoxelForgeStudio" /
            "preferences.ini";
#else
    if (const auto configHome = Environment("XDG_CONFIG_HOME"))
        return std::filesystem::path(*configHome) / "VoxelForgeStudio" /
            "preferences.ini";
    if (const auto home = Environment("HOME"))
        return std::filesystem::path(*home) / ".config" / "VoxelForgeStudio" /
            "preferences.ini";
#endif
    return {};
}

bool ProjectDialogPreferences::Save()
{
    lastError_.clear();
    if (storageFilePath_.empty()) return true;
    std::error_code error;
    if (!storageFilePath_.parent_path().empty())
        std::filesystem::create_directories(storageFilePath_.parent_path(), error);
    if (error)
    {
        lastError_ = "Unable to create the preferences directory.";
        return false;
    }
    std::filesystem::path temporaryFilePath = storageFilePath_;
    temporaryFilePath += ".tmp";
    std::ofstream output(temporaryFilePath, std::ios::trunc);
    if (!output)
    {
        lastError_ = "Unable to write project dialog preferences.";
        return false;
    }
    output << "last_create_parent=" << ToUtf8(lastCreateParent_) << '\n'
           << "last_open_directory=" << ToUtf8(lastOpenDirectory_) << '\n'
           << "first_creation_completed="
           << (firstCreationCompleted_ ? 1 : 0) << '\n';
    if (!output)
    {
        lastError_ = "Unable to finish writing project dialog preferences.";
        return false;
    }
    output.close();
    if (!output)
    {
        lastError_ = "Unable to finish writing project dialog preferences.";
        return false;
    }

    std::filesystem::rename(temporaryFilePath, storageFilePath_, error);
    if (error)
    {
        error.clear();
        std::filesystem::copy_file(
            temporaryFilePath,
            storageFilePath_,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error)
        {
            lastError_ = "Unable to replace project dialog preferences.";
            return false;
        }
        std::error_code cleanupError;
        std::filesystem::remove(temporaryFilePath, cleanupError);
    }
    return true;
}

std::filesystem::path ProjectDialogPreferences::ExistingDirectoryOrEmpty(
    const std::filesystem::path& path)
{
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path, error) && !error
        ? std::filesystem::absolute(path, error).lexically_normal()
        : std::filesystem::path{};
}

} // namespace VoxelForge::Editor
