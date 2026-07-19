#include "Input/EditorInputService.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

EditorInputCommand Resolve(
    const EditorInputService& service,
    const EditorInputKey key,
    const EditorCommandAvailability availability,
    const bool control = false,
    const bool shift = false)
{
    EditorInputFrame frame;
    frame.SetPressed(key);
    frame.Control = control;
    frame.Shift = shift;
    return service.Resolve(frame, availability);
}

void TestCommandsAndBindings()
{
    const EditorInputService service;
    Require(!service.HasBindingConflicts(),
        "Default keyboard bindings contain a conflict.");
    Require(service.Bindings().size() == 20U,
        "The expected command bindings are incomplete.");
    Require(service.CommandName(EditorInputCommand::ToolPencil) ==
            "Tool.Pencil" &&
        service.CommandName(EditorInputCommand::InteractionCancel) ==
            "Interaction.Cancel" &&
        service.CommandName(static_cast<EditorInputCommand>(255)).empty(),
        "Command names or unknown-command handling are invalid.");
    Require(service.ShortcutLabel(EditorInputCommand::ToolFill) == "F" &&
        service.ShortcutLabel(EditorInputCommand::ToolBox) == "B" &&
        service.ShortcutLabel(EditorInputCommand::ToolLine) == "L" &&
        service.ShortcutLabel(EditorInputCommand::ToolSphere) == "S" &&
        service.ShortcutLabel(EditorInputCommand::ToolSelection) == "V" &&
        service.ShortcutLabel(EditorInputCommand::ToolMove) == "M" &&
        service.ShortcutLabel(EditorInputCommand::ToolDuplicate) == "D" &&
        service.ShortcutLabel(EditorInputCommand::ToolRotate) == "R" &&
        service.ShortcutLabel(EditorInputCommand::ToolMirror) == "H" &&
        service.ShortcutLabel(EditorInputCommand::RotateLeft) == "Q" &&
        service.ShortcutLabel(EditorInputCommand::RotateRight) == "Shift+Q" &&
        service.ShortcutLabel(EditorInputCommand::MirrorX) == "X" &&
        service.ShortcutLabel(EditorInputCommand::MirrorZ) == "Z" &&
        service.ShortcutLabel(EditorInputCommand::TransformApply) == "Enter" &&
        service.ShortcutLabel(EditorInputCommand::FileSave) == "Ctrl+S",
        "Shortcut labels do not reflect the real bindings.");
}

void TestToolSelection()
{
    const EditorInputService service;
    const EditorCommandAvailability available{
        true, true, true, true, true, true, true, true, true, true,
        true, true};
    Require(Resolve(service, EditorInputKey::P, available) ==
            EditorInputCommand::ToolPencil &&
        Resolve(service, EditorInputKey::E, available) ==
            EditorInputCommand::ToolEraser &&
        Resolve(service, EditorInputKey::F, available) ==
            EditorInputCommand::ToolFill &&
        Resolve(service, EditorInputKey::B, available) ==
            EditorInputCommand::ToolBox &&
        Resolve(service, EditorInputKey::L, available) ==
            EditorInputCommand::ToolLine &&
        Resolve(service, EditorInputKey::S, available) ==
            EditorInputCommand::ToolSphere &&
        Resolve(service, EditorInputKey::V, available) ==
            EditorInputCommand::ToolSelection &&
        Resolve(service, EditorInputKey::M, available) ==
            EditorInputCommand::ToolMove &&
        Resolve(service, EditorInputKey::D, available) ==
            EditorInputCommand::ToolDuplicate &&
        Resolve(service, EditorInputKey::R, available) ==
            EditorInputCommand::ToolRotate &&
        Resolve(service, EditorInputKey::H, available) ==
            EditorInputCommand::ToolMirror &&
        Resolve(service, EditorInputKey::Q, available) ==
            EditorInputCommand::RotateLeft &&
        Resolve(service, EditorInputKey::Q, available, false, true) ==
            EditorInputCommand::RotateRight &&
        Resolve(service, EditorInputKey::X, available) ==
            EditorInputCommand::MirrorX &&
        Resolve(service, EditorInputKey::Z, available) ==
            EditorInputCommand::MirrorZ &&
        Resolve(service, EditorInputKey::Enter, available) ==
            EditorInputCommand::TransformApply,
        "A tool shortcut resolves to the wrong command.");
    Require(Resolve(service, EditorInputKey::S, available, true) ==
            EditorInputCommand::FileSave,
        "Ctrl+S conflicts with the Sphere shortcut.");
}

void TestSaveUndoRedoAndCancel()
{
    const EditorInputService service;
    const EditorCommandAvailability available{true, true, true, true, true};
    Require(Resolve(service, EditorInputKey::S, available, true) ==
            EditorInputCommand::FileSave &&
        Resolve(service, EditorInputKey::Z, available, true) ==
            EditorInputCommand::EditUndo &&
        Resolve(service, EditorInputKey::Y, available, true) ==
            EditorInputCommand::EditRedo &&
        Resolve(service, EditorInputKey::Escape, available) ==
            EditorInputCommand::InteractionCancel,
        "Save, Undo, Redo, or Cancel resolves incorrectly.");
}

void TestProtectedInputContexts()
{
    const EditorInputService service;
    const EditorCommandAvailability available{true, true, true, true, true};
    EditorInputFrame frame;
    frame.SetPressed(EditorInputKey::P);
    const auto blocked = [&service, &available, &frame]()
    {
        Require(service.Resolve(frame, available) == EditorInputCommand::None,
            "A shortcut interrupted protected user input.");
    };
    frame.TextInputActive = true; blocked();
    frame.TextInputActive = false; frame.DialogTextInputActive = true; blocked();
    frame.DialogTextInputActive = false; frame.RenameActive = true; blocked();
    frame.RenameActive = false; frame.NumericInputActive = true; blocked();
    frame.NumericInputActive = false; frame.PopupOpen = true; blocked();
}

void TestAvailabilityAndUnknownCommands()
{
    const EditorInputService service;
    const EditorCommandAvailability unavailable{};
    Require(Resolve(service, EditorInputKey::P, unavailable) ==
            EditorInputCommand::None &&
        Resolve(service, EditorInputKey::S, unavailable, true) ==
            EditorInputCommand::None &&
        !service.IsAvailable(
            static_cast<EditorInputCommand>(255), unavailable),
        "An unavailable or unknown command was accepted.");

    EditorCommandAvailability partial;
    partial.HasDocument = true;
    Require(service.IsAvailable(EditorInputCommand::ToolPencil, partial) &&
        !service.IsAvailable(EditorInputCommand::ToolMove, partial) &&
        !service.IsAvailable(EditorInputCommand::ToolDuplicate, partial) &&
        !service.IsAvailable(EditorInputCommand::FileSave, partial) &&
        !service.IsAvailable(EditorInputCommand::EditUndo, partial) &&
        !service.IsAvailable(EditorInputCommand::EditRedo, partial) &&
        !service.IsAvailable(EditorInputCommand::InteractionCancel, partial),
        "Command availability ignores editor state.");
    partial.CanMoveSelection = true;
    partial.CanDuplicateSelection = true;
    partial.CanRotateSelection = true;
    partial.CanAdjustRotation = true;
    partial.CanMirrorSelection = true;
    partial.CanAdjustMirror = true;
    Require(service.IsAvailable(EditorInputCommand::ToolMove, partial) &&
        Resolve(service, EditorInputKey::M, partial) ==
            EditorInputCommand::ToolMove &&
        service.IsAvailable(EditorInputCommand::ToolDuplicate, partial) &&
        Resolve(service, EditorInputKey::D, partial) ==
            EditorInputCommand::ToolDuplicate &&
        service.IsAvailable(EditorInputCommand::ToolRotate, partial) &&
        Resolve(service, EditorInputKey::R, partial) ==
            EditorInputCommand::ToolRotate &&
        Resolve(service, EditorInputKey::Q, partial) ==
            EditorInputCommand::RotateLeft &&
        Resolve(service, EditorInputKey::Q, partial, false, true) ==
            EditorInputCommand::RotateRight &&
        service.IsAvailable(EditorInputCommand::ToolMirror, partial) &&
        Resolve(service, EditorInputKey::H, partial) ==
            EditorInputCommand::ToolMirror &&
        Resolve(service, EditorInputKey::X, partial) ==
            EditorInputCommand::MirrorX &&
        Resolve(service, EditorInputKey::Z, partial) ==
            EditorInputCommand::MirrorZ &&
        Resolve(service, EditorInputKey::Enter, partial) ==
            EditorInputCommand::None,
        "Transform preview commands require selection but Apply requires preview.");
    partial.CanApplyTransform = true;
    Require(
        Resolve(service, EditorInputKey::Enter, partial) ==
            EditorInputCommand::TransformApply,
        "Transform Apply stayed unavailable for a valid preview.");
}
}

int main()
{
    try
    {
        TestCommandsAndBindings();
        TestToolSelection();
        TestSaveUndoRedoAndCancel();
        TestProtectedInputContexts();
        TestAvailabilityAndUnknownCommands();
        std::cout << "Editor input service tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Editor input service tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
