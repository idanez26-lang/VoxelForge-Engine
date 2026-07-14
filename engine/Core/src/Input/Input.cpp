#include "VoxelForge/Core/Input/Input.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

namespace VoxelForge::Core
{

bool Input::IsKeyPressed(VoxelForge::KeyCode keyCode) noexcept
{
    int keyCount = 0;
    const bool* keyboardState = SDL_GetKeyboardState(&keyCount);
    const int index = static_cast<int>(keyCode);

    return keyboardState != nullptr && index >= 0 && index < keyCount &&
           keyboardState[index];
}

bool Input::IsMouseButtonPressed(VoxelForge::MouseCode button) noexcept
{
    const SDL_MouseButtonFlags state = SDL_GetMouseState(nullptr, nullptr);
    const auto mask = SDL_BUTTON_MASK(static_cast<unsigned int>(button));
    return (state & mask) != 0U;
}

MousePosition Input::GetMousePosition() noexcept
{
    MousePosition position;
    SDL_GetMouseState(&position.X, &position.Y);
    return position;
}

MousePosition Input::GetMouseDelta() noexcept
{
    MousePosition delta;
    SDL_GetRelativeMouseState(&delta.X, &delta.Y);
    return delta;
}

} // namespace VoxelForge::Core
