#include "EditorDialogModel.h"

#include <algorithm>

namespace VoxelForge::Editor
{

EditorDialogLayout EditorDialogModel::CalculateLayout(
    const float viewportWidth) noexcept
{
    constexpr float minimumWidth = 360.0F;
    constexpr float preferredWidth = 520.0F;
    constexpr float edgeMargin = 32.0F;
    constexpr float horizontalPadding = 40.0F;
    constexpr float buttonWidth = 112.0F;

    const float safeViewportWidth = std::max(1.0F, viewportWidth);
    const float availableWidth = std::max(
        1.0F, safeViewportWidth - edgeMargin * 2.0F);
    const float width = std::min(
        preferredWidth,
        std::max(std::min(minimumWidth, availableWidth), availableWidth));

    return {
        width,
        std::max(1.0F, width - horizontalPadding),
        std::min(buttonWidth, std::max(72.0F, width * 0.28F))};
}

EditorDialogShortcut EditorDialogModel::ResolveShortcut(
    const bool enterPressed,
    const bool escapePressed,
    const bool confirmationEnabled) noexcept
{
    if (escapePressed) return EditorDialogShortcut::Cancel;
    if (enterPressed && confirmationEnabled)
        return EditorDialogShortcut::Confirm;
    return EditorDialogShortcut::None;
}

bool EditorDialogModel::ShouldFocusFirstField(
    const bool popupAppearing,
    const bool hasEditableField) noexcept
{
    return popupAppearing && hasEditableField;
}

}
