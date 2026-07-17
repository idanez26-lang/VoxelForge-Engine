#pragma once

#include "EditorDialogModel.h"

#include <string_view>

namespace VoxelForge::Editor
{

class EditorDialogStyle final
{
public:
    static bool BeginPopup(
        const char* popupName,
        EditorDialogIntent intent,
        std::string_view title,
        std::string_view description,
        bool hasEditableField = false);
    static void EndPopup();

    static void FullWidthField();
    static void DrawMessage(
        std::string_view message,
        EditorDialogIntent intent = EditorDialogIntent::Warning);
    static void BeginActions();
    static bool ActionButton(
        const char* label,
        bool primary,
        bool enabled = true,
        bool destructive = false);
    static EditorDialogShortcut Shortcuts(bool confirmationEnabled = true);
};

}
