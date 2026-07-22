#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class WelcomeProjectAvailability
{
    Available,
    Missing
};

struct WelcomeProjectEntry
{
    std::filesystem::path ProjectFilePath;
    std::string Name;
    WelcomeProjectAvailability Availability =
        WelcomeProjectAvailability::Missing;

    [[nodiscard]] bool CanOpen() const noexcept
    {
        return Availability == WelcomeProjectAvailability::Available;
    }

    [[nodiscard]] bool CanDelete() const noexcept { return CanOpen(); }
};

struct WelcomeScreenLayout
{
    float ContentWidth = 1.0F;
    float TopPadding = 0.0F;
    float CardHeight = 112.0F;
};

class WelcomeScreenModel final
{
public:
    [[nodiscard]] static std::vector<WelcomeProjectEntry> BuildEntries(
        std::span<const std::filesystem::path> recentProjects);

    [[nodiscard]] static WelcomeScreenLayout CalculateLayout(
        float availableWidth,
        float availableHeight) noexcept;

    [[nodiscard]] static bool ShowEditorPanels(bool hasActiveProject) noexcept
    {
        return hasActiveProject;
    }
};

} // namespace VoxelForge::Editor
