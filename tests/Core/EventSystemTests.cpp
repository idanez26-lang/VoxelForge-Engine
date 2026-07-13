#include "VoxelForge/Core/Event/EventDispatcher.h"
#include "VoxelForge/Core/Event/KeyboardEvent.h"
#include "VoxelForge/Core/Event/MouseEvent.h"
#include "VoxelForge/Core/Event/WindowEvent.h"
#include <cassert>

int main()
{
    using namespace VoxelForge;
    WindowResizeEvent resizeEvent(1280u, 720u);
    Event& event = resizeEvent;
    EventDispatcher dispatcher(event);
    const bool dispatched = dispatcher.Dispatch<WindowResizeEvent>(
        [](WindowResizeEvent& e) { return e.GetWidth() == 1280u && e.GetHeight() == 720u; });
    assert(dispatched);
    assert(event.Handled);
    assert(event.IsInCategory(EventCategoryApplication));
    KeyPressedEvent keyEvent(65u, false);
    assert(keyEvent.IsInCategory(EventCategoryKeyboard));
    MouseButtonPressedEvent mouseEvent(1u);
    assert(mouseEvent.IsInCategory(EventCategoryMouseButton));
    return 0;
}
