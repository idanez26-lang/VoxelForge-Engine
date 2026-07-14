#pragma once

#include "VoxelForge/Core/Event/Event.h"
#include "VoxelForge/Core/Input/MouseCodes.h"

#include <sstream>
#include <string>

namespace VoxelForge
{

class MouseMovedEvent final : public Event
{
public:
    MouseMovedEvent(float x, float y) noexcept : m_MouseX(x), m_MouseY(y) {}
    [[nodiscard]] float GetX() const noexcept { return m_MouseX; }
    [[nodiscard]] float GetY() const noexcept { return m_MouseY; }

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << m_MouseX << ", " << m_MouseY;
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(MouseMoved)
    VF_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse)

private:
    float m_MouseX;
    float m_MouseY;
};

class MouseScrolledEvent final : public Event
{
public:
    MouseScrolledEvent(float xOffset, float yOffset) noexcept
        : m_XOffset(xOffset), m_YOffset(yOffset) {}

    [[nodiscard]] float GetXOffset() const noexcept { return m_XOffset; }
    [[nodiscard]] float GetYOffset() const noexcept { return m_YOffset; }

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << m_XOffset << ", " << m_YOffset;
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(MouseScrolled)
    VF_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryMouse)

private:
    float m_XOffset;
    float m_YOffset;
};

class MouseButtonEvent : public Event
{
public:
    [[nodiscard]] MouseCode GetMouseButton() const noexcept { return m_Button; }
    VF_EVENT_CLASS_CATEGORY(
        EventCategoryInput | EventCategoryMouse | EventCategoryMouseButton)

protected:
    explicit MouseButtonEvent(MouseCode button) noexcept : m_Button(button) {}

private:
    MouseCode m_Button;
};

class MouseButtonPressedEvent final : public MouseButtonEvent
{
public:
    explicit MouseButtonPressedEvent(MouseCode button) noexcept
        : MouseButtonEvent(button) {}

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << static_cast<int>(GetMouseButton());
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(MouseButtonPressed)
};

class MouseButtonReleasedEvent final : public MouseButtonEvent
{
public:
    explicit MouseButtonReleasedEvent(MouseCode button) noexcept
        : MouseButtonEvent(button) {}

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << static_cast<int>(GetMouseButton());
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(MouseButtonReleased)
};

} // namespace VoxelForge
