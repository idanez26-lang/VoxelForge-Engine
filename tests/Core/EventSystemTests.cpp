#include "VoxelForge/Core/Event/EventDispatcher.h"
#include "VoxelForge/Core/Event/FileDropEvent.h"
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
    KeyPressedEvent keyEvent(KeyCode::A, false);
    assert(keyEvent.IsInCategory(EventCategoryKeyboard));
    MouseButtonPressedEvent mouseEvent(MouseCode::Left);
    assert(mouseEvent.IsInCategory(EventCategoryMouseButton));
    std::filesystem::path droppedPath = "castle.vox";
    FileDropFileEvent dropEvent(droppedPath, 12.0F, 34.0F);
    droppedPath = "changed-after-event.vox";
    assert(dropEvent.GetPath() == std::filesystem::path("castle.vox"));
    assert(dropEvent.GetX() == 12.0F && dropEvent.GetY() == 34.0F);
    assert(dropEvent.IsInCategory(EventCategoryApplication));
    return 0;
}
