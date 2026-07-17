#include "Dialogs/EditorDialogModel.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main()
{
    using namespace VoxelForge::Editor;

    const EditorDialogLayout narrow =
        EditorDialogModel::CalculateLayout(320.0F);
    const EditorDialogLayout wide =
        EditorDialogModel::CalculateLayout(1920.0F);
    Require(narrow.Width > 0.0F && narrow.Width <= 320.0F,
        "A narrow dialog must remain visible inside its viewport.");
    Require(wide.Width >= narrow.Width && wide.Width <= 520.0F,
        "A wide dialog must remain readable without becoming excessive.");
    Require(narrow.ContentWidth > 0.0F && wide.ContentWidth > 0.0F,
        "Responsive content widths must always be positive.");
    Require(narrow.ButtonWidth > 0.0F && wide.ButtonWidth > 0.0F,
        "Responsive button widths must always be positive.");

    Require(EditorDialogModel::ResolveShortcut(true, false, true) ==
            EditorDialogShortcut::Confirm,
        "Enter must confirm an enabled primary action.");
    Require(EditorDialogModel::ResolveShortcut(true, false, false) ==
            EditorDialogShortcut::None,
        "Enter must not confirm a disabled primary action.");
    Require(EditorDialogModel::ResolveShortcut(true, true, true) ==
            EditorDialogShortcut::Cancel,
        "Escape must take priority and cancel safely.");
    Require(EditorDialogModel::ShouldFocusFirstField(true, true),
        "An editable dialog must focus its first field when it opens.");
    Require(!EditorDialogModel::ShouldFocusFirstField(false, true) &&
            !EditorDialogModel::ShouldFocusFirstField(true, false),
        "Focus must not be stolen after opening or without a field.");

    std::cout << "Modern dialog model tests passed.\n";
    return EXIT_SUCCESS;
}
