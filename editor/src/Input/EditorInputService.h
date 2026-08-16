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
    // VF-UX-TOOLS : selection de FAMILLE depuis la barre de gauche. Distinctes
    // de ToolPencil / ToolEraser / ToolFill, qui restent les raccourcis
    // historiques et imposent en plus une action. Une famille, elle, ne touche
    // jamais a l'action courante : les deux axes sont orthogonaux, comme dans
    // MagicaVoxel. Placees a l'interieur de la plage ToolPencil..ToolAlign pour
    // que IsToolCommand les reconnaisse.
    ToolFamilyPencil,
    ToolFamilyGeometry,
    ToolFamilyFace,
    ToolFamilySurface,
    ToolFamilyFill,
    ToolEraser,
    ToolFill,
    ToolBox,
    ToolLine,
    ToolSphere,
    ToolSelection,
    ToolMove,
    ToolDuplicate,
    ToolRotate,
    ToolMirror,
    ToolScale,
    ToolWrap,
    ToolAlign,
    RotateLeft,
    RotateRight,
    MirrorX,
    MirrorZ,
    ScaleX,
    ScaleY,
    ScaleZ,
    ScaleUniform,
    AlignLeft,
    AlignRight,
    AlignBottom,
    AlignTop,
    AlignFront,
    AlignBack,
    TransformApply,
    StampCycleMirror,
    StampResetTransform,
    StampToggleRotationStep,
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
    V,
    M,
    D,
    H,
    K,
    U,
    X,
    Q,
    R,
    A,
    W,
    Enter,
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
    bool CanMoveSelection = false;
    bool CanDuplicateSelection = false;
    bool CanRotateSelection = false;
    bool CanAdjustRotation = false;
    bool CanMirrorSelection = false;
    bool CanAdjustMirror = false;
    bool CanScaleSelection = false;
    bool CanAdjustScale = false;
    bool CanAlignSelection = false;
    bool CanAdjustAlign = false;
    bool CanApplyTransform = false;
    bool CanAdjustStampTransform = false;
};

struct SmartBrushSizeInputFrame final
{
    float Wheel = 0.0F;
    bool LeftControl = false;
    bool RightControl = false;
    bool HasDocument = false;
    bool SmartToolActive = false;
    bool ViewportHovered = false;
    bool ViewportFocused = false;
    bool MouseCapturedByOtherWidget = false;
    bool ModalOpen = false;
    bool DragDropActive = false;
    bool IncompatibleInteraction = false;
    int CurrentSize = 1;
};

struct SmartBrushSizeInputResult final
{
    int Size = 1;
    bool Changed = false;
    bool ConsumeWheel = false;
};

class EditorInputService final
{
public:
    static constexpr std::size_t BindingCount = 30U;

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
    [[nodiscard]] static SmartBrushSizeInputResult ResolveSmartBrushSize(
        const SmartBrushSizeInputFrame& frame) noexcept;

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
         false, true, false, "Shift+F"},
        {EditorInputCommand::ToolBox, EditorInputKey::B,
         false, false, false, "B"},
        {EditorInputCommand::ToolLine, EditorInputKey::L,
         false, false, false, "L"},
        {EditorInputCommand::ToolSphere, EditorInputKey::S,
         false, false, false, "S"},
        {EditorInputCommand::ToolSelection, EditorInputKey::V,
         false, false, false, "V"},
        {EditorInputCommand::ToolMove, EditorInputKey::M,
         false, false, false, "M"},
        {EditorInputCommand::ToolDuplicate, EditorInputKey::D,
         false, false, false, "D"},
        {EditorInputCommand::ToolRotate, EditorInputKey::R,
         false, false, false, "R"},
        {EditorInputCommand::ToolMirror, EditorInputKey::H,
         false, false, false, "H"},
        {EditorInputCommand::ToolScale, EditorInputKey::K,
         false, false, false, "K"},
        {EditorInputCommand::ToolWrap, EditorInputKey::W,
         false, false, false, "W"},
        {EditorInputCommand::ToolAlign, EditorInputKey::A,
         false, true, false, "Shift+A"},
        {EditorInputCommand::RotateLeft, EditorInputKey::Q,
         false, false, false, "Q"},
        {EditorInputCommand::RotateRight, EditorInputKey::Q,
         false, true, false, "Shift+Q"},
        {EditorInputCommand::MirrorX, EditorInputKey::X,
         false, false, false, "X"},
        {EditorInputCommand::MirrorZ, EditorInputKey::Z,
         false, false, false, "Z"},
        {EditorInputCommand::ScaleX, EditorInputKey::X,
         false, false, false, "X"},
        {EditorInputCommand::ScaleY, EditorInputKey::Y,
         false, false, false, "Y"},
        {EditorInputCommand::ScaleZ, EditorInputKey::Z,
         false, false, false, "Z"},
        {EditorInputCommand::ScaleUniform, EditorInputKey::U,
         false, false, false, "U"},
        {EditorInputCommand::TransformApply, EditorInputKey::Enter,
         false, false, false, "Enter"},
        {EditorInputCommand::StampCycleMirror, EditorInputKey::M,
         false, false, false, "M"},
        {EditorInputCommand::StampResetTransform, EditorInputKey::M,
         false, true, false, "Shift+M"},
        // STAMP-25 : Shift+E est libre dans tout l'editeur, donc aucun
        // conflit de memoire musculaire avec les touches d'outil.
        {EditorInputCommand::StampToggleRotationStep, EditorInputKey::E,
         false, true, false, "Shift+E"}
    }};
};

} // namespace VoxelForge::Editor
