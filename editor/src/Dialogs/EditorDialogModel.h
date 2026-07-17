#pragma once

namespace VoxelForge::Editor
{

enum class EditorDialogIntent
{
    Create,
    Open,
    Import,
    Save,
    Warning,
    Destructive,
    Information
};

enum class EditorDialogShortcut
{
    None,
    Confirm,
    Cancel
};

struct EditorDialogLayout
{
    float Width = 0.0F;
    float ContentWidth = 0.0F;
    float ButtonWidth = 0.0F;
};

class EditorDialogModel final
{
public:
    static EditorDialogLayout CalculateLayout(float viewportWidth) noexcept;
    static EditorDialogShortcut ResolveShortcut(
        bool enterPressed,
        bool escapePressed,
        bool confirmationEnabled) noexcept;
    static bool ShouldFocusFirstField(
        bool popupAppearing,
        bool hasEditableField) noexcept;
};

}
