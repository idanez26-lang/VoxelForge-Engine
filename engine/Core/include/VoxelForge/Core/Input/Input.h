#pragma once

#include "VoxelForge/Core/Input/KeyCodes.h"
#include "VoxelForge/Core/Input/MouseCodes.h"

namespace VoxelForge::Core
{

struct MousePosition
{
    float X = 0.0F;
    float Y = 0.0F;
};

class Input final
{
public:
    Input() = delete;

    [[nodiscard]] static bool IsKeyPressed(VoxelForge::KeyCode keyCode) noexcept;
    [[nodiscard]] static bool IsMouseButtonPressed(VoxelForge::MouseCode button) noexcept;
    [[nodiscard]] static MousePosition GetMousePosition() noexcept;
    [[nodiscard]] static MousePosition GetMouseDelta() noexcept;
};

} // namespace VoxelForge::Core
