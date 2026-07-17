#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace VoxelForge::Editor
{

enum class EditorInputCommand : std::uint8_t
{
    None,
    ToolPencil,
    ToolEraser,
    ToolFill,
    ToolBox,
    ToolLine,
    ToolSphere,
    FileSave,
    EditUndo,
    EditRedo,
    InteractionCancel,
    Count
};

enum class EditorInputKey : std::uint8_t
{
    P,
    E,
    F,
    B,
    L,
    S,
    Z,
    Y,
    Escape,
    Count
};

struct EditorInputBinding final
{
    EditorInputCommand Command = EditorInputCommand::None;
    EditorInputKey Key = EditorInputKey::P;
    bool Control = false;
    bool Shift = false;
    bool Alt = false;
    std::string_view Shortcut;
};

struct EditorInputFrame final
{
    std::array<bool, static_cast<std::size_t>(EditorInputKey::Count)> Pressed{};
    bool Control = false;
    bool Shift = false;
    bool Alt = false;
    bool TextInputActive = false;
    bool DialogTextInputActive = false;
    bool RenameActive = false;
    bool NumericInputActive = false;
    bool PopupOpen = false;

    void SetPressed(EditorInputKey key, bool pressed = true) noexcept;
    [[nodiscard]] bool IsPressed(EditorInputKey key) const noexcept;
    [[nodiscard]] bool IsBlocked() const noexcept;
};

struct EditorCommandAvailability final
{
    bool HasDocument = false;
    bool CanSave = false;
    bool CanUndo = false;
    bool CanRedo = false;
    bool CanCancelInteraction = false;
};

class EditorInputService final
{
public:
    static constexpr std::size_t BindingCount = 10U;

    [[nodiscard]] EditorInputCommand Resolve(
        const EditorInputFrame& frame,
        const EditorCommandAvailability& availability) const noexcept;
    [[nodiscard]] bool IsAvailable(
        EditorInputCommand command,
        const EditorCommandAvailability& availability) const noexcept;
    [[nodiscard]] std::string_view ShortcutLabel(
        EditorInputCommand command) const noexcept;
    [[nodiscard]] std::string_view CommandName(
        EditorInputCommand command) const noexcept;
    [[nodiscard]] bool HasBindingConflicts() const noexcept;
    [[nodiscard]] const std::array<EditorInputBinding, BindingCount>&
        Bindings() const noexcept;

private:
    std::array<EditorInputBinding, BindingCount> bindings_{{
        {EditorInputCommand::InteractionCancel, EditorInputKey::Escape,
         false, false, false, "Esc"},
        {EditorInputCommand::FileSave, EditorInputKey::S,
         true, false, false, "Ctrl+S"},
        {EditorInputCommand::EditUndo, EditorInputKey::Z,
         true, false, false, "Ctrl+Z"},
        {EditorInputCommand::EditRedo, EditorInputKey::Y,
         true, false, false, "Ctrl+Y"},
        {EditorInputCommand::ToolPencil, EditorInputKey::P,
         false, false, false, "P"},
        {EditorInputCommand::ToolEraser, EditorInputKey::E,
         false, false, false, "E"},
        {EditorInputCommand::ToolFill, EditorInputKey::F,
         false, false, false, "F"},
        {EditorInputCommand::ToolBox, EditorInputKey::B,
         false, false, false, "B"},
        {EditorInputCommand::ToolLine, EditorInputKey::L,
         false, false, false, "L"},
        {EditorInputCommand::ToolSphere, EditorInputKey::S,
         false, false, false, "S"}
    }};
};

} // namespace VoxelForge::Editor
