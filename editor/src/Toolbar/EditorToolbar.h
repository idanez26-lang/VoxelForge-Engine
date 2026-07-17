#pragma once

#include "EditorToolbarModel.h"

#include <functional>

namespace VoxelForge::Editor
{

struct EditorToolbarCallbacks final
{
    std::function<void(EditorInputCommand)> ExecuteCommand;
};

class EditorToolbar final
{
public:
    static void Draw(
        const EditorToolbarState& state,
        const EditorInputService& inputService,
        const EditorToolbarCallbacks& callbacks);
};

} // namespace VoxelForge::Editor
