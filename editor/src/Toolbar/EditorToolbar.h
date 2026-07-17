#pragma once

#include "EditorToolbarModel.h"

#include <functional>

namespace VoxelForge::Editor
{

struct EditorToolbarCallbacks final
{
    std::function<void()> Save;
    std::function<void(ActiveVoxelTool)> SelectTool;
};

class EditorToolbar final
{
public:
    static void Draw(
        const EditorToolbarState& state,
        const EditorToolbarCallbacks& callbacks);
};

} // namespace VoxelForge::Editor
