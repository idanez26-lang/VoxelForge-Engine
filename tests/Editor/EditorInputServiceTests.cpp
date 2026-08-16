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
    Require(service.Bindings().size() == 30U,
        "The expected command bindings are incomplete.");
    // VF-WRAP-V1 : W selectionne Wrap, disponible avec une selection.
    Require(Resolve(service, EditorInputKey::W, {true, false, false, false,
            false, true}) == EditorInputCommand::ToolWrap,
        "W does not resolve to Tool.Wrap with a selection.");
    Require(service.CommandName(EditorInputCommand::ToolPencil) ==
            "Tool.Pencil" &&
        service.CommandName(EditorInputCommand::InteractionCancel) ==
            "Interaction.Cancel" &&
        service.CommandName(EditorInputCommand::StampCycleMirror) ==
            "Stamp.CycleMirror" &&
        service.CommandName(EditorInputCommand::StampResetTransform) ==
            "Stamp.ResetTransform" &&
        service.CommandName(
            EditorInputCommand::StampToggleRotationStep) ==
            "Stamp.ToggleRotationStep" &&
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
        service.ShortcutLabel(EditorInputCommand::StampCycleMirror) == "M" &&
        service.ShortcutLabel(EditorInputCommand::StampResetTransform) ==
            "Shift+M" &&
        service.ShortcutLabel(
            EditorInputCommand::StampToggleRotationStep) == "Shift+E" &&
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

void TestActiveStampTransformContext()
{
    const EditorInputService service;
    EditorCommandAvailability stamp;
    stamp.HasDocument = true;
    stamp.CanCancelInteraction = true;
    stamp.CanAdjustRotation = true;
    stamp.CanAdjustMirror = true;
    stamp.CanApplyTransform = true;
    stamp.CanAdjustStampTransform = true;

    Require(
        Resolve(service, EditorInputKey::Q, stamp) ==
                EditorInputCommand::RotateLeft &&
            Resolve(service, EditorInputKey::Q, stamp, false, true) ==
                EditorInputCommand::RotateRight &&
            Resolve(service, EditorInputKey::X, stamp) ==
                EditorInputCommand::MirrorX &&
            Resolve(service, EditorInputKey::Z, stamp) ==
                EditorInputCommand::MirrorZ &&
            Resolve(service, EditorInputKey::M, stamp) ==
                EditorInputCommand::StampCycleMirror &&
            Resolve(service, EditorInputKey::M, stamp, false, true) ==
                EditorInputCommand::StampResetTransform &&
            Resolve(service, EditorInputKey::E, stamp, false, true) ==
                EditorInputCommand::StampToggleRotationStep &&
            Resolve(service, EditorInputKey::Enter, stamp) ==
                EditorInputCommand::TransformApply &&
            Resolve(service, EditorInputKey::Escape, stamp) ==
                EditorInputCommand::InteractionCancel,
        "Active Stamp placement shortcuts do not own their transform commands.");
    EditorCommandAvailability noStamp;
    noStamp.HasDocument = true;
    Require(
        Resolve(service, EditorInputKey::E, noStamp, false, true) ==
            EditorInputCommand::None,
        "The rotation step switch must stay inert outside a Stamp placement.");
    Require(
        Resolve(service, EditorInputKey::P, stamp) ==
                EditorInputCommand::None &&
            Resolve(service, EditorInputKey::R, stamp) ==
                EditorInputCommand::None &&
            !service.IsAvailable(EditorInputCommand::ToolMove, stamp) &&
            !service.IsAvailable(EditorInputCommand::ToolPencil, stamp),
        "A tool shortcut interrupted the active Stamp placement session.");
}

void TestSmartBrushSizeResolver()
{
    SmartBrushSizeInputFrame frame;
    frame.Wheel = 1.0F;
    frame.LeftControl = true;
    frame.HasDocument = true;
    frame.SmartToolActive = true;
    frame.ViewportHovered = true;
    frame.ViewportFocused = true;
    frame.CurrentSize = 1;

    SmartBrushSizeInputResult result =
        EditorInputService::ResolveSmartBrushSize(frame);
    Require(result.Size == 2 && result.Changed && result.ConsumeWheel,
        "Size 1 plus positive Ctrl+wheel did not resolve to size 2.");

    frame.CurrentSize = 2;
    frame.Wheel = -1.0F;
    result = EditorInputService::ResolveSmartBrushSize(frame);
    Require(result.Size == 1 && result.Changed && result.ConsumeWheel,
        "Size 2 plus negative Ctrl+wheel did not resolve to size 1.");

    frame.LeftControl = false;
    frame.RightControl = true;
    frame.CurrentSize = 4;
    frame.Wheel = -3.0F;
    result = EditorInputService::ResolveSmartBrushSize(frame);
    Require(result.Size == 1 && result.Changed && result.ConsumeWheel,
        "Right Ctrl or multi-step wheel input resolves incorrectly.");

    frame.CurrentSize = 16;
    frame.Wheel = 1.0F;
    result = EditorInputService::ResolveSmartBrushSize(frame);
    Require(result.Size == 16 && !result.Changed && result.ConsumeWheel,
        "A size limit must still consume an accepted Ctrl+wheel input.");

    frame.CurrentSize = 1;
    frame.Wheel = -1.0F;
    result = EditorInputService::ResolveSmartBrushSize(frame);
    Require(result.Size == 1 && !result.Changed && result.ConsumeWheel,
        "The lower size limit must still consume an accepted Ctrl+wheel input.");

    const auto requireBlocked = [&frame](const std::string_view message)
    {
        const SmartBrushSizeInputResult blocked =
            EditorInputService::ResolveSmartBrushSize(frame);
        Require(blocked.Size == 1 && !blocked.Changed && !blocked.ConsumeWheel,
            message);
    };
    frame.RightControl = false;
    requireBlocked("Wheel input without Ctrl was incorrectly consumed.");
    frame.RightControl = true;

    frame.HasDocument = false;
    requireBlocked("A missing document enabled brush size input.");
    frame.HasDocument = true;
    frame.SmartToolActive = false;
    requireBlocked("An inactive Smart tool enabled brush size input.");
    frame.SmartToolActive = true;
    frame.ViewportHovered = false;
    requireBlocked("A non-hovered viewport enabled brush size input.");
    frame.ViewportHovered = true;
    frame.ViewportFocused = false;
    requireBlocked("A non-focused viewport enabled brush size input.");
    frame.ViewportFocused = true;
    frame.MouseCapturedByOtherWidget = true;
    requireBlocked("Another ImGui widget capture was ignored.");
    frame.MouseCapturedByOtherWidget = false;
    frame.ModalOpen = true;
    requireBlocked("A modal did not block brush size input.");
    frame.ModalOpen = false;
    frame.DragDropActive = true;
    requireBlocked("Drag and drop did not block brush size input.");
    frame.DragDropActive = false;
    frame.IncompatibleInteraction = true;
    requireBlocked("An incompatible interaction did not block brush size input.");
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
        TestActiveStampTransformContext();
        TestSmartBrushSizeResolver();
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
