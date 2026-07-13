#pragma once
#include "VoxelForge/Core/Event/Event.h"
#include <cstdint>
#include <sstream>
#include <string>

namespace VoxelForge
{
class WindowCloseEvent final : public Event
{
public:
    VF_EVENT_CLASS_TYPE(WindowClose)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
};

class WindowResizeEvent final : public Event
{
public:
    WindowResizeEvent(std::uint32_t width, std::uint32_t height) noexcept
        : m_Width(width), m_Height(height) {}
    [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_Width; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_Height; }
    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream s; s << GetName() << ": " << m_Width << "x" << m_Height; return s.str();
    }
    VF_EVENT_CLASS_TYPE(WindowResize)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
private:
    std::uint32_t m_Width;
    std::uint32_t m_Height;
};

class WindowFocusEvent final : public Event
{
public:
    VF_EVENT_CLASS_TYPE(WindowFocus)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
};

class WindowLostFocusEvent final : public Event
{
public:
    VF_EVENT_CLASS_TYPE(WindowLostFocus)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
};

class WindowMovedEvent final : public Event
{
public:
    WindowMovedEvent(std::int32_t x, std::int32_t y) noexcept : m_X(x), m_Y(y) {}
    [[nodiscard]] std::int32_t GetX() const noexcept { return m_X; }
    [[nodiscard]] std::int32_t GetY() const noexcept { return m_Y; }
    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream s; s << GetName() << ": " << m_X << ", " << m_Y; return s.str();
    }
    VF_EVENT_CLASS_TYPE(WindowMoved)
    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication)
private:
    std::int32_t m_X;
    std::int32_t m_Y;
};
} // namespace VoxelForge
