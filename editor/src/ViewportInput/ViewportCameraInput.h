#pragma once

#include <cstdint>

namespace VoxelForge::Editor
{

struct ViewportCameraInput final
{
    bool ShortcutsEnabled = false;
    bool FrameShortcutPressed = false;
    bool ResetShortcutPressed = false;
    std::uint8_t LeftClickCount = 0U;
};

struct ViewportCameraActions final
{
    bool FrameRequested = false;
    bool ResetRequested = false;
};

// Pointer clicks are deliberately not camera commands. LeftClickCount remains
// part of the input contract so the no-op behavior is explicit and testable.
[[nodiscard]] ViewportCameraActions ResolveViewportCameraActions(
    const ViewportCameraInput& input) noexcept;

} // namespace VoxelForge::Editor
