#pragma once
#include "VoxelForge/Core/Event/Event.h"
#include <type_traits>
#include <utility>

namespace VoxelForge
{
class EventDispatcher
{
public:
    explicit EventDispatcher(Event& event) noexcept : m_Event(event) {}

    template <typename TEvent, typename TFunction>
    bool Dispatch(TFunction&& function)
    {
        static_assert(std::is_base_of_v<Event, TEvent>, "TEvent must derive from Event.");
        if (m_Event.GetEventType() != TEvent::GetStaticType())
            return false;

        const bool handled = static_cast<bool>(
            std::forward<TFunction>(function)(static_cast<TEvent&>(m_Event)));
        m_Event.Handled = m_Event.Handled || handled;
        return true;
    }

private:
    Event& m_Event;
};
} // namespace VoxelForge
