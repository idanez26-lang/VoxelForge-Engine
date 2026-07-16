#pragma once

#include <optional>

namespace VoxelForge::Editor
{

enum class ProjectShortcut
{
    NewProject,
    OpenProject,
    SaveProject,
    SaveAs,
    CloseProject
};

struct ShortcutContext final
{
    bool TextInput = false;
    bool ActiveItem = false;
    bool PopupOpen = false;
    bool HasProject = false;
};

[[nodiscard]] inline bool CanRunProjectShortcut(
    const ProjectShortcut shortcut,
    const ShortcutContext& context) noexcept
{
    if (context.TextInput || context.ActiveItem || context.PopupOpen) return false;
    return shortcut != ProjectShortcut::SaveProject &&
            shortcut != ProjectShortcut::CloseProject
        ? true : context.HasProject;
}

enum class DestructiveAction
{
    CloseProject,
    OpenProject,
    CreateProject,
    CreateVoxelModel,
    ExitApplication,
    ReplaceVoxelModel
};

class DirtyActionConfirmation final
{
public:
    [[nodiscard]] bool Request(
        const DestructiveAction action,
        const bool hasUnsavedModel) noexcept
    {
        if (!hasUnsavedModel) return true;
        if (pending_) return false;
        pending_ = action;
        return false;
    }

    [[nodiscard]] std::optional<DestructiveAction> Discard() noexcept
    {
        const auto result = pending_;
        pending_.reset();
        return result;
    }

    [[nodiscard]] std::optional<DestructiveAction>
    ContinueAfterSuccessfulSave() noexcept
    {
        const auto result = pending_;
        pending_.reset();
        return result;
    }

    void Cancel() noexcept { pending_.reset(); }
    [[nodiscard]] bool IsPending() const noexcept { return pending_.has_value(); }

private:
    std::optional<DestructiveAction> pending_;
};

} // namespace VoxelForge::Editor
