#pragma once
#include <cstdint>
#include <string>

namespace VoxelForge
{
enum class EventType : std::uint8_t
{
    None = 0,
    WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
    AppTick, AppUpdate, AppRender,
    KeyPressed, KeyReleased, KeyTyped,
    MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled,
    FileDropBegin, FileDropFile, FileDropPosition, FileDropComplete
};

enum EventCategory : std::uint32_t
{
    EventCategoryNone = 0,
    EventCategoryApplication = 1u << 0u,
    EventCategoryInput = 1u << 1u,
    EventCategoryKeyboard = 1u << 2u,
    EventCategoryMouse = 1u << 3u,
    EventCategoryMouseButton = 1u << 4u
};

class Event
{
public:
    virtual ~Event() = default;
    [[nodiscard]] virtual EventType GetEventType() const noexcept = 0;
    [[nodiscard]] virtual const char* GetName() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t GetCategoryFlags() const noexcept = 0;
    [[nodiscard]] virtual std::string ToString() const { return GetName(); }
    [[nodiscard]] bool IsInCategory(EventCategory category) const noexcept
    {
        return (GetCategoryFlags() & static_cast<std::uint32_t>(category)) != 0u;
    }
    bool Handled = false;
};

#define VF_EVENT_CLASS_TYPE(type) \
    static EventType GetStaticType() noexcept { return EventType::type; } \
    EventType GetEventType() const noexcept override { return GetStaticType(); } \
    const char* GetName() const noexcept override { return #type; }

#define VF_EVENT_CLASS_CATEGORY(flags) \
    std::uint32_t GetCategoryFlags() const noexcept override \
    { return static_cast<std::uint32_t>(flags); }

} // namespace VoxelForge
