#include "Console/EditorConsoleService.h"

#include <utility>

namespace VoxelForge::Editor
{

EditorConsoleService::EditorConsoleService(
    const std::size_t maximumMessageCount)
    : maximumMessageCount_(
          maximumMessageCount == 0U ? 1U : maximumMessageCount)
{
}

void EditorConsoleService::AddMessage(std::string message)
{
    if (messages_.size() >= maximumMessageCount_)
    {
        messages_.erase(messages_.begin());
    }

    messages_.push_back(std::move(message));
    visible_ = true;
}

const std::vector<std::string>& EditorConsoleService::Messages()
    const noexcept
{
    return messages_;
}

void EditorConsoleService::Clear() noexcept
{
    messages_.clear();
}

bool EditorConsoleService::IsVisible() const noexcept { return visible_; }

void EditorConsoleService::SetVisible(const bool visible) noexcept
{
    visible_ = visible;
}

bool* EditorConsoleService::VisibilityFlag() noexcept { return &visible_; }

std::size_t EditorConsoleService::MaximumMessageCount() const noexcept
{
    return maximumMessageCount_;
}

} // namespace VoxelForge::Editor
