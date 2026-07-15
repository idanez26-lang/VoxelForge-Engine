#include "EditorWindowTitle.h"

#include <string>

int main()
{
    using VoxelForge::Editor::FormatEditorWindowTitle;

    if (FormatEditorWindowTitle() != "VoxelForge Studio")
    {
        return 1;
    }

    const std::string expectedProjectTitle =
        "VoxelForge Studio \xE2\x80\x94 ArtisanProject";

    if (FormatEditorWindowTitle("ArtisanProject") != expectedProjectTitle)
    {
        return 2;
    }

    return 0;
}
