#pragma once

#include <string>
#include <string_view>

namespace VoxelForge::Editor
{

inline std::string FormatEditorWindowTitle(
    const std::string_view projectName = {})
{
    constexpr std::string_view BaseTitle = "VoxelForge Studio";

    if (projectName.empty())
    {
        return std::string(BaseTitle);
    }

    return std::string(BaseTitle) + " \xE2\x80\x94 " +
        std::string(projectName);
}

} // namespace VoxelForge::Editor
