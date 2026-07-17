#include "EditorInputService.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::size_t Index(const EditorInputKey key) noexcept
{
    return static_cast<std::size_t>(key);
}

bool IsToolCommand(const EditorInputCommand command) noexcept
{
    return command >= EditorInputCommand::ToolPencil &&
        command <= EditorInputCommand::ToolSphere;
}

bool SameChord(
    const EditorInputBinding& left,
    const EditorInputBinding& right) noexcept
{
    return left.Key == right.Key && left.Control == right.Control &&
        left.Shift == right.Shift && left.Alt == right.Alt;
}
}

void EditorInputFrame::SetPressed(
    const EditorInputKey key, const bool pressed) noexcept
{
    if (key == EditorInputKey::Count) return;
    Pressed[Index(key)] = pressed;
}

bool EditorInputFrame::IsPressed(const EditorInputKey key) const noexcept
{
    return key != EditorInputKey::Count && Pressed[Index(key)];
}

bool EditorInputFrame::IsBlocked() const noexcept
{
    return TextInputActive || DialogTextInputActive || RenameActive ||
        NumericInputActive || PopupOpen;
}

EditorInputCommand EditorInputService::Resolve(
    const EditorInputFrame& frame,
    const EditorCommandAvailability& availability) const noexcept
{
    if (frame.IsBlocked()) return EditorInputCommand::None;
    const auto binding = std::find_if(
        bindings_.begin(), bindings_.end(),
        [this, &frame, &availability](const EditorInputBinding& candidate)
        {
            return frame.IsPressed(candidate.Key) &&
                frame.Control == candidate.Control &&
                frame.Shift == candidate.Shift &&
                frame.Alt == candidate.Alt &&
                IsAvailable(candidate.Command, availability);
        });
    return binding == bindings_.end()
        ? EditorInputCommand::None : binding->Command;
}

bool EditorInputService::IsAvailable(
    const EditorInputCommand command,
    const EditorCommandAvailability& availability) const noexcept
{
    if (IsToolCommand(command)) return availability.HasDocument;
    switch (command)
    {
    case EditorInputCommand::FileSave: return availability.CanSave;
    case EditorInputCommand::EditUndo: return availability.CanUndo;
    case EditorInputCommand::EditRedo: return availability.CanRedo;
    case EditorInputCommand::InteractionCancel:
        return availability.CanCancelInteraction;
    case EditorInputCommand::None:
    case EditorInputCommand::Count:
    default: return false;
    }
}

std::string_view EditorInputService::ShortcutLabel(
    const EditorInputCommand command) const noexcept
{
    const auto binding = std::find_if(
        bindings_.begin(), bindings_.end(),
        [command](const EditorInputBinding& candidate)
        {
            return candidate.Command == command;
        });
    return binding == bindings_.end() ? std::string_view{} : binding->Shortcut;
}

std::string_view EditorInputService::CommandName(
    const EditorInputCommand command) const noexcept
{
    switch (command)
    {
    case EditorInputCommand::ToolPencil: return "Tool.Pencil";
    case EditorInputCommand::ToolEraser: return "Tool.Eraser";
    case EditorInputCommand::ToolFill: return "Tool.Fill";
    case EditorInputCommand::ToolBox: return "Tool.Box";
    case EditorInputCommand::ToolLine: return "Tool.Line";
    case EditorInputCommand::ToolSphere: return "Tool.Sphere";
    case EditorInputCommand::FileSave: return "File.Save";
    case EditorInputCommand::EditUndo: return "Edit.Undo";
    case EditorInputCommand::EditRedo: return "Edit.Redo";
    case EditorInputCommand::InteractionCancel: return "Interaction.Cancel";
    case EditorInputCommand::None:
    case EditorInputCommand::Count:
    default: return {};
    }
}

bool EditorInputService::HasBindingConflicts() const noexcept
{
    for (std::size_t left = 0U; left < bindings_.size(); ++left)
        for (std::size_t right = left + 1U; right < bindings_.size(); ++right)
            if (SameChord(bindings_[left], bindings_[right])) return true;
    return false;
}

const std::array<EditorInputBinding, EditorInputService::BindingCount>&
EditorInputService::Bindings() const noexcept
{
    return bindings_;
}

} // namespace VoxelForge::Editor
