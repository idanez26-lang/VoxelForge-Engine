#include "Welcome/WelcomeScreenModel.h"

#include <algorithm>

namespace VoxelForge::Editor
{

std::vector<WelcomeProjectEntry> WelcomeScreenModel::BuildEntries(
    const std::span<const std::filesystem::path> recentProjects)
{
    std::vector<WelcomeProjectEntry> entries;
    entries.reserve(recentProjects.size());
    for (const std::filesystem::path& path : recentProjects)
    {
        std::error_code error;
        const bool available = std::filesystem::is_regular_file(path, error) &&
            !error && path.extension() == ".vfproject";
        entries.push_back({
            path,
            path.stem().string(),
            available ? WelcomeProjectAvailability::Available
                      : WelcomeProjectAvailability::Missing});
    }
    return entries;
}

WelcomeScreenLayout WelcomeScreenModel::CalculateLayout(
    const float availableWidth,
    const float availableHeight) noexcept
{
    const float safeWidth = std::max(1.0F, availableWidth);
    const float safeHeight = std::max(1.0F, availableHeight);
    return {
        std::min(safeWidth,
            std::clamp(safeWidth - 48.0F, 300.0F, 920.0F)),
        std::clamp(safeHeight * 0.08F, 20.0F, 84.0F),
        safeWidth < 560.0F ? 132.0F : 112.0F};
}

} // namespace VoxelForge::Editor
