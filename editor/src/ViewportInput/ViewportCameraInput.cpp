#include "ViewportInput/ViewportCameraInput.h"

namespace VoxelForge::Editor
{

ViewportCameraActions ResolveViewportCameraActions(
    const ViewportCameraInput& input) noexcept
{
    if (!input.ShortcutsEnabled)
    {
        return {};
    }

    // A simple click and every click in a double-click sequence belong to
    // selection or editing. Only explicit keyboard shortcuts control navigation.
    static_cast<void>(input.LeftClickCount);
    return {
        input.FocusShortcutPressed,
        input.ResetShortcutPressed};
}

} // namespace VoxelForge::Editor
