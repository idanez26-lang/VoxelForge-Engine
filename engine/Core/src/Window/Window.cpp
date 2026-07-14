#include "VoxelForge/Core/Window/Window.h"

#include "Window/SDLWindow.h"

#include <memory>

namespace VoxelForge::Core
{

std::unique_ptr<Window> Window::Create(
    const WindowSpecification& specification)
{
    return std::make_unique<SDLWindow>(specification);
}

} // namespace VoxelForge::Core
