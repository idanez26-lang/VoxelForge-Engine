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
    Require(service.Bindings().size() == 26U,
        "The expected command bindings are incomplete.");
    Require(service.CommandName(EditorInputCommand::ToolPencil) ==
            "Tool.Pencil" &&
        service.CommandName(EditorInputCommand::InteractionCancel) ==
            "Interaction.Cancel" &&
        service.CommandName(static_cast<EditorInputCommand>(255)).empty(),
        "Command names or unknown-command handling are invalid.");
    Require(service.ShortcutLabel(EditorInputCommand::ToolFill) == "Shift+F" &&
        service.ShortcutLabel(EditorInputCommand::ToolBox) == "B" &&
        service.ShortcutLabel(EditorInputCommand::ToolLine) == "L" &&
        service.ShortcutLabel(EditorInputCommand::ToolSphere) == "S" &&
        service.ShortcutLabel(EditorInputCommand::ToolSelection) == "V" &&
        service.ShortcutLabel(EditorInputCommand::ToolMove) == "M" &&
        service.ShortcutLabel(EditorInputCommand::ToolDuplicate) == "D" &&
        service.ShortcutLabel(EditorInputCommand::ToolRotate) == "R" &&
        service.ShortcutLabel(EditorInputCommand::ToolMirror) == "H" &&
        service.ShortcutLabel(EditorInputCommand::ToolScale) == "K" &&
        service.ShortcutLabel(EditorInputCommand::ToolAlign) == "Shift+A" &&
        service.ShortcutLabel(EditorInputCommand::RotateLeft) == "Q" &&
        service.ShortcutLabel(EditorInputCommand::RotateRight) == "Shift+Q" &&
        service.ShortcutLabel(EditorInputCommand::MirrorX) == "X" &&
        service.ShortcutLabel(EditorInputCommand::MirrorZ) == "Z" &&
        service.ShortcutLabel(EditorInputCommand::ScaleX) == "X" &&
        service.ShortcutLabel(EditorInputCommand::ScaleY) == "Y" &&
        service.ShortcutLabel(EditorInputCommand::ScaleZ) == "Z" &&
        service.ShortcutLabel(EditorInputCommand::ScaleUniform) == "U" &&
        service.ShortcutLabel(EditorInputCommand::TransformApply) == "Enter" &&
        service.ShortcutLabel(EditorInputCommand::FileSave) == "Ctrl+S",
        "Shortcut labels do not reflect the real bindings.");
}

void TestToolSelection()
{
    const EditorInputService service;
    EditorCommandAvailability available;
    available.HasDocument = true;
    available.CanSave = true;
    available.CanUndo = true;
    available.CanRedo = true;
    available.CanCancelInteraction = true;
    available.CanMoveSelection = true;
    available.CanDuplicateSelection = true;
    available.CanRotateSelection = true;
    available.CanAdjustRotation = true;
    available.CanMirrorSelection = true;
    available.CanAdjustMirror = true;
    available.CanScaleSelection = true;
    available.CanAlignSelection = true;
    available.CanAdjustAlign = true;
    available.CanApplyTransform = true;
    Require(Resolve(service, EditorInputKey::P, available) ==
            EditorInputCommand::ToolPencil &&
        Resolve(service, EditorInputKey::E, available) ==
            EditorInputCommand::ToolEraser &&
        Resolve(service, EditorInputKey::F, available, false, true) ==
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
        Resolve(service, EditorInputKey::K, available) ==
            EditorInputCommand::ToolScale &&
        Resolve(service, EditorInputKey::A, available, false, true) ==
            EditorInputCommand::ToolAlign &&
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
    available.CanAdjustMirror = false;
    available.CanAdjustScale = true;
    Require(Resolve(service, EditorInputKey::X, available) ==
            EditorInputCommand::ScaleX &&
        Resolve(service, EditorInputKey::Y, available) ==
            EditorInputCommand::ScaleY &&
        Resolve(service, EditorInputKey::Z, available) ==
            EditorInputCommand::ScaleZ &&
        Resolve(service, EditorInputKey::U, available) ==
            EditorInputCommand::ScaleUniform,
        "Contextual Scale bindings do not resolve to Scale commands.");
    Require(Resolve(service, EditorInputKey::S, available, true) ==
            EditorInputCommand::FileSave,
        "Ctrl+S conflicts with the Sphere shortcut.");
    Require(Resolve(service, EditorInputKey::F, available) ==
            EditorInputCommand::None,
        "F is no longer reserved for viewport focus.");
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
    partial.CanScaleSelection = true;
    partial.CanAlignSelection = true;
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
        service.IsAvailable(EditorInputCommand::ToolScale, partial) &&
        Resolve(service, EditorInputKey::K, partial) ==
            EditorInputCommand::ToolScale &&
        service.IsAvailable(EditorInputCommand::ToolAlign, partial) &&
        Resolve(service, EditorInputKey::A, partial, false, true) ==
            EditorInputCommand::ToolAlign &&
        Resolve(service, EditorInputKey::Enter, partial) ==
            EditorInputCommand::None,
        "Transform preview commands require selection but Apply requires preview.");
    partial.CanAdjustMirror = false;
    partial.CanAdjustScale = true;
    Require(Resolve(service, EditorInputKey::X, partial) ==
            EditorInputCommand::ScaleX &&
        Resolve(service, EditorInputKey::Y, partial) ==
            EditorInputCommand::ScaleY &&
        Resolve(service, EditorInputKey::Z, partial) ==
            EditorInputCommand::ScaleZ &&
        Resolve(service, EditorInputKey::U, partial) ==
            EditorInputCommand::ScaleUniform,
        "Scale mode commands are not gated by Scale context.");
    partial.CanAdjustScale = false;
    partial.CanAdjustAlign = true;
    Require(service.IsAvailable(EditorInputCommand::AlignLeft, partial) &&
        service.IsAvailable(EditorInputCommand::AlignRight, partial) &&
        service.IsAvailable(EditorInputCommand::AlignBottom, partial) &&
        service.IsAvailable(EditorInputCommand::AlignTop, partial) &&
        service.IsAvailable(EditorInputCommand::AlignFront, partial) &&
        service.IsAvailable(EditorInputCommand::AlignBack, partial) &&
        service.CommandName(EditorInputCommand::AlignLeft) == "Align.Left" &&
        service.CommandName(EditorInputCommand::AlignBack) == "Align.Back",
        "Align direction commands are not gated by Align context.");
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
