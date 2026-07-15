#include "Platform/ProjectFolderOpener.h"

#include <SDL3/SDL.h>

namespace VoxelForge::Editor
{
namespace
{
bool IsAsciiAlphaNumeric(const unsigned char value)
{
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9');
}

}

std::string BuildProjectFolderUri(
    const std::filesystem::path& absoluteFolder)
{
    const std::u8string utf8 = absoluteFolder.u8string();
    std::string uri = "file://";
    if (!utf8.empty() && utf8.front() != u8'/')
    {
        uri.push_back('/');
    }
    constexpr char Hex[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < utf8.size(); ++index)
    {
        const char8_t character = utf8[index];
        const unsigned char value = static_cast<unsigned char>(character);
        const bool windowsDriveColon = value == ':' && index == 1U &&
            IsAsciiAlphaNumeric(static_cast<unsigned char>(utf8[0]));
        if (IsAsciiAlphaNumeric(value) || value == '-' || value == '_' ||
            value == '.' || value == '~' || value == '/' || windowsDriveColon)
        {
            uri.push_back(static_cast<char>(value));
        }
        else
        {
            uri.push_back('%');
            uri.push_back(Hex[value >> 4U]);
            uri.push_back(Hex[value & 0x0FU]);
        }
    }
    return uri;
}

namespace
{
class SDLProjectFolderOpener final : public ProjectFolderOpener
{
public:
    bool Open(const std::filesystem::path& folder, std::string& error) override
    {
        std::error_code filesystemError;
        if (!std::filesystem::is_directory(folder, filesystemError) ||
            filesystemError)
        {
            error = "The active project folder is unavailable.";
            return false;
        }
        const std::filesystem::path absoluteFolder =
            std::filesystem::absolute(folder, filesystemError);
        if (filesystemError)
        {
            error = "The active project folder path cannot be normalized.";
            return false;
        }
        if (!SDL_OpenURL(BuildProjectFolderUri(absoluteFolder).c_str()))
        {
            error = SDL_GetError();
            if (error.empty()) error = "The project folder could not be opened.";
            return false;
        }
        error.clear();
        return true;
    }
};
}

std::unique_ptr<ProjectFolderOpener> CreateSDLProjectFolderOpener()
{
    return std::make_unique<SDLProjectFolderOpener>();
}

} // namespace VoxelForge::Editor
